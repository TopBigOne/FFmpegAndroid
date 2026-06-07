
/**
 * Note: Handler of Audio Render
 * Date: 2026/6/6
 * Author: frank
 */

#include "AudioRenderHandler.h"

#include <utility>

#include "common/NextConfig.h"
#include "NextMessage.h"

#define TAG "AuRenderHandler"

AudioRenderHandler::AudioRenderHandler(sp<AudioDecodeHandler> &audioDecodeHandler,
                                       const sp<PlayerLink> &pLink,
                                       NotifyCallback notifyCb,
                                       const char *threadName)
        : BaseThread(threadName),
          mPlayerLink(pLink),
          mAudioDecodeHandler(audioDecodeHandler),
          mNotifyCb(std::move(notifyCb)) {
    mFrameBuffer = std::make_unique<FrameBuffer>();
}

AudioRenderHandler::~AudioRenderHandler() {
    mAudioRender.reset();
    if (mAudioCallback) {
        mAudioCallback.reset();
    }
    if (mFrameBuffer) {
        mFrameBuffer->Release();
    }
    swr_free(&mSwrContext);
    av_freep(&mAudioNewBuf);
    if (mAudioBuf1Size > 0) {
        av_freep(&mAudioBuf1);
    }
    mAudioDecodeHandler.reset();
    mMetaData.reset();
}

void AudioRenderHandler::SetConfig(const sp<GeneralConfig> &config) {
    std::unique_lock<std::mutex> lock(mLock);
    mGeneralConfig = config;
}

int AudioRenderHandler::Prepare(sp<MetaData> &metadata) {
    int ret = RESULT_OK;
    mMetaData = metadata;
    mAudioRender = AudioRenderFactory::CreateAudioRender();

    ret = Init();
    if (ret != RESULT_OK) {
        NotifyListener(MSG_ON_ERROR, ERROR_RENDER_AUDIO_INIT);
        return ret;
    }
    return ret;
}

int AudioRenderHandler::Init() {
    if (!mAudioRender) {
        NEXT_LOGE(TAG, "Init failed, audio render is null\n");
        return ERROR_PLAYER_NOT_INIT;
    }
    if (bPaused) {
        mAudioRender->PauseAudio(true);
    }

    int streamIndex = mMetaData->audio_index;

    for (auto & it : mMetaData->track_info) {
        if (it.stream_type == AVMEDIA_TYPE_AUDIO && it.stream_index == streamIndex) {
            mBytesPerSec = av_samples_get_buffer_size(nullptr, it.channels, it.sample_rate,
                                                      AVSampleFormat(it.sample_fmt), 1);
            // set audio parameters
            mDesired.channels       = it.channels;
            mDesired.sample_rate    = it.sample_rate;
            mDesired.channel_layout = static_cast<AVChannelLayout *>(malloc(sizeof(AVChannelLayout)));
            av_channel_layout_default(mDesired.channel_layout, it.channels);

            mDesired.silence = 0;
            mDesired.format  = (AVSampleFormat) it.sample_fmt;
            mDesired.samples =
                    std::max(kAudioMinBufferSize,
                             2 << av_log2(mDesired.sample_rate /
                                          mAudioRender->GetAudioCallBack()));
            NEXT_LOGI(TAG,
                      "OpenAudio wanted channels:%d, sampleRate:%d, format:%s\n",
                      mDesired.channels, mDesired.sample_rate,
                      av_get_sample_fmt_name(static_cast<AVSampleFormat>(mDesired.format)));
            break;
        }
    }

    mAudioCallback = std::make_unique<AudioCallbackClass>(
            reinterpret_cast<void *>(this), AudioDataCallback);
    int ret = mAudioRender->OpenAudio(mDesired, mObtained, mAudioCallback);
    if (ret < 0) {
        NotifyListener(MSG_ON_ERROR, ERROR_RENDER_AUDIO_INIT, static_cast<int32_t>(ret));
        NEXT_LOGE(TAG, "OpenAudio error:%d", ret);
        return ERROR_RENDER_AUDIO_INIT;
    }

    if (mObtained.channels != mDesired.channels) {
        mObtained.channel_layout = mDesired.channel_layout;
    }
    mBytesPerSec = av_samples_get_buffer_size(nullptr, mObtained.channels, mObtained.sample_rate,
                                              (AVSampleFormat) mObtained.format, 1);
    mAudioRender->SetDefaultDelay(
            (static_cast<double>(2 * mObtained.size)) / mBytesPerSec);
    mAudioDelay = mAudioRender->GetDelay();
    mCurrentInfo = mObtained;

    char buf[256] = {0};
    av_channel_layout_describe(mObtained.channel_layout, buf, sizeof(buf));
    NEXT_LOGD(TAG,"AudioRender init channels:%d, sampleRate:%d, "
            "channelLayout:%s, format:%s\n",
            mObtained.channels, mObtained.sample_rate, buf,
            av_get_sample_fmt_name(static_cast<AVSampleFormat>(mObtained.format)));

    return RESULT_OK;
}

void AudioRenderHandler::ExecuteTask() {

}

int AudioRenderHandler::StartRender() {
    std::unique_lock<std::mutex> lock(mLock);
    bPaused = false;
    mPlayerLink->audio_clock->SetClock(mPlayerLink->audio_clock->GetClock());
    mPlayerLink->audio_clock->SetPause(false);
    if (mAudioRender) {
        mAudioRender->PauseAudio(false);
    }
    return RESULT_OK;
}

int AudioRenderHandler::PauseRender() {
    std::unique_lock<std::mutex> lock(mLock);
    bPaused = true;
    if (mAudioRender) {
        mAudioRender->PauseAudio(true);
    }
    mPlayerLink->audio_clock->SetPause(true);
    return RESULT_OK;
}

int AudioRenderHandler::ResampleAudioData(std::unique_ptr<FrameBuffer> &buffer) {
    int dataSize = 0;
    int resampledSize = 0;
    int wanted_nb_samples;

    if (!buffer->channel[0]) {
        NEXT_LOGE(TAG, "Invalid audio data\n");
        return ERROR_PLAYER_TRY_AGAIN;
    }
    dataSize =
            av_samples_get_buffer_size(nullptr, buffer->numChannels, buffer->sampleSize,
                                       AVSampleFormat(buffer->format), 1);

    wanted_nb_samples = buffer->sampleSize;
    if ((AVSampleFormat) buffer->format != mCurrentInfo.format ||
//        av_channel_layout_compare(buffer->channelLayout, mCurrentInfo.channel_layout) || // todo
        buffer->sampleRate != mCurrentInfo.sample_rate ||
        (wanted_nb_samples != buffer->sampleSize && !mSwrContext)) {
        AVDictionary *swr_opts = nullptr;
        swr_free(&mSwrContext);
        int ret = swr_alloc_set_opts2(
                &mSwrContext, mObtained.channel_layout,
                (AVSampleFormat) mObtained.format,
                mObtained.sample_rate, buffer->channelLayout,
                (enum AVSampleFormat) buffer->format, buffer->sampleRate, 0, nullptr);
        if (ret < 0) {
            NEXT_LOGE(TAG,
                      "Cannot create sample rate converter for conversion of %d Hz "
                      "%s %d channels to %d Hz %s %d channels!\n",
                      buffer->sampleRate,
                      av_get_sample_fmt_name((enum AVSampleFormat) buffer->format),
                      buffer->numChannels, mObtained.sample_rate,
                      av_get_sample_fmt_name((AVSampleFormat) mObtained.format),
                      mObtained.channels);
            return ERROR_RENDER_AUDIO_SWR;
        }

        av_opt_set_dict(mSwrContext, &swr_opts);
        av_dict_free(&swr_opts);

        if (swr_init(mSwrContext) < 0) {
            NEXT_LOGE(TAG, "Create converter fail, from %d Hz "
                      "%s %d channels to %d Hz %s %d channels\n",
                      buffer->sampleRate,
                      av_get_sample_fmt_name((enum AVSampleFormat) buffer->format),
                      buffer->numChannels, mObtained.sample_rate,
                      av_get_sample_fmt_name((AVSampleFormat) mObtained.format),
                      mObtained.channels);
            swr_free(&mSwrContext);
            return ERROR_RENDER_AUDIO_SWR;
        }

        NEXT_LOGD(TAG,
                  "Created converter from %d Hz %s %d ch to %d Hz %s %d ch\n",
                  buffer->sampleRate,
                  av_get_sample_fmt_name((enum AVSampleFormat) buffer->format),
                  buffer->numChannels, mObtained.sample_rate,
                  av_get_sample_fmt_name((AVSampleFormat) mObtained.format),
                  mObtained.channels);

//        mCurrentInfo.channel_layout = buffer->channelLayout; // todo
        mCurrentInfo.channels    = buffer->numChannels;
        mCurrentInfo.sample_rate = buffer->sampleRate;
        mCurrentInfo.format      = (AVSampleFormat) buffer->format;
    }
    if (mSwrContext) {
        const auto **in = (const uint8_t **) &buffer->channel;
        uint8_t **out   = &mAudioBuf1;
        int out_count   = static_cast<int>(static_cast<int64_t>(wanted_nb_samples) *
                mObtained.sample_rate / buffer->sampleRate + 256);
        int out_size = av_samples_get_buffer_size(
                nullptr, mObtained.channels, out_count,
                (AVSampleFormat) mObtained.format, 0);
        int len2;
        if (out_size < 0) {
            NEXT_LOGE(TAG, "av_samples_get_buffer_size() failed\n");
            return ERROR_RENDER_AUDIO_SWR;
        }
        if (wanted_nb_samples != buffer->sampleSize) {
            if (swr_set_compensation(mSwrContext,
                                     (wanted_nb_samples - buffer->sampleSize) *
                                     mObtained.sample_rate / buffer->sampleRate,
                                     wanted_nb_samples * mObtained.sample_rate /
                                     buffer->sampleRate) < 0) {
                NEXT_LOGE(TAG, "swr_set_compensation() failed\n");
                return ERROR_RENDER_AUDIO_SWR;
            }
        }
        av_fast_malloc(&mAudioBuf1, &mAudioBuf1Size, out_size);

        if (!mAudioBuf1) {
            NEXT_LOGE(TAG, "malloc mAudioBuf1 failed\n");
            return ERROR_RENDER_AUDIO_SWR;
        }

        len2 = swr_convert(mSwrContext, out, out_count, in, buffer->sampleSize);
        if (len2 < 0) {
            NEXT_LOGE(TAG, "swr_convert failed, ret=%s\n", av_err2str(len2));
            return ERROR_RENDER_AUDIO_SWR;
        }
        if (len2 == out_count) {
            NEXT_LOGW(TAG, "audio buffer is probably too small\n");
            if (swr_init(mSwrContext) < 0)
                swr_free(&mSwrContext);
        }
        mAudioBuf = mAudioBuf1;
        int bytes_per_sample = av_get_bytes_per_sample((AVSampleFormat) mObtained.format);
        resampledSize = len2 * mObtained.channels * bytes_per_sample;
        mBytesPerSec =
                buffer->sampleRate * buffer->numChannels * bytes_per_sample;
    } else {
        mAudioBuf = buffer->channel[0];
        mAudioBuf1 = mAudioBuf;
        resampledSize = dataSize;
    }
    if (!bFirstFrameDecoded) {
        bFirstFrameDecoded = true;
        NotifyListener(MSG_AUDIO_DECODE_START);
    }
    return resampledSize;
}

void AudioRenderHandler::GetAudioData(char *data, int &len) {
    if (!data || len <= 0) {
        return;
    }
    std::unique_lock<std::mutex> lock(mLock);
    int left_size = len;
    int offset = 0;
    double current_pts = 0;
    memset(data, 0, len);

    if (bAbort) {
        return;
    }

    if (bVolumeChanged) {
        bVolumeChanged = false;
        if (mAudioRender) {
            NEXT_LOGD(TAG, "volume changed %f %f\n", mLeftVolume, mRightVolume);
            mAudioRender->SetPlaybackVolume((mLeftVolume + mRightVolume) / 2);
        }
    }

    while (left_size > 0 && !bAbort) {
        if (mLastReadPos == 0) {
            mFrameBuffer->Release();
            mAudioBufSize = 0;

            std::unique_ptr<FrameBuffer> buffer;
            lock.unlock();
            int32_t ret = ReadFrame(buffer);
            if (ret != RESULT_OK || !buffer) {
                return;
            }

            // resample
            int resampled_data_size = ResampleAudioData(buffer);
            if (resampled_data_size <= 0) {
                NEXT_LOGW(TAG, "Failed to resample audio data, ret=%d\n", resampled_data_size);
                return;
            }

            lock.lock();
            mFrameBuffer = std::move(buffer);

            mAudioBuf = reinterpret_cast<uint8_t*>(mAudioBuf1);
            mAudioBufSize = resampled_data_size;
        }

        if (mFrameBuffer->serial != mAudioDecodeHandler->GetSerial()) {
            mLastReadPos = 0;
            mAudioBufSize = 0;
            if (mAudioRender) {
                mAudioRender->FlushAudio();
            }
            return;
        }

        int write_size = std::min(left_size, mAudioBufSize - mLastReadPos);
        if (mMute.load()) {
            memset(data + offset, 0, write_size);
        } else {
            memcpy(data + offset, mAudioBuf + mLastReadPos, write_size);
        }

        mLastReadPos += write_size;
        offset += write_size;
        left_size -= write_size;

        if (mBytesPerSec > 0) {
            current_pts = static_cast<double>(mFrameBuffer->pts) +
                          static_cast<double>(mLastReadPos) / mBytesPerSec * 1000;
        } else {
            current_pts = static_cast<double>(mFrameBuffer->pts);
        }

        if (mLastReadPos == mAudioBufSize) {
            mLastReadPos = 0;
        }
    }

    lock.unlock();

    if (current_pts > 0) {
        mPlayerLink->audio_clock->SetClock(current_pts / 1000.0 - mAudioDelay);
        if (mPlayerLink->audio_clock->GetClockSerial() != mFrameBuffer->serial) {
            mPlayerLink->audio_clock->SetClockSerial(mFrameBuffer->serial);
        }
    }

    if (!mPlayerLink->first_audio_rendered) {
        mPlayerLink->first_audio_rendered = true;
        NotifyListener(MSG_AUDIO_RENDER_START);
    }

    if (mFrameBuffer->serial >= 0 &&
        mPlayerLink->last_audio_seek_serial == mFrameBuffer->serial) {
        int latest_audio_seek_load_serial =
                mPlayerLink->last_audio_seek_serial.exchange(-1, std::memory_order_seq_cst);
        if (latest_audio_seek_load_serial == mFrameBuffer->serial) {
            bool is_master_audio = (getMasterSyncType(mPlayerLink) == CLOCK_AUDIO);
            NotifyListener(MSG_AUDIO_SEEK_RENDER_START, is_master_audio ? 1 : 0);
        }
    }
}

void AudioRenderHandler::AudioDataCallback(void *opaque, char *data, int &len) {
    auto *thiz = reinterpret_cast<AudioRenderHandler *>(opaque);
    thiz->GetAudioData(data, len);
}

int AudioRenderHandler::ReadFrame(std::unique_ptr<FrameBuffer> &buffer) {
    if (!mAudioDecodeHandler)
        return ERROR_PARSE_NOT_INIT;
    return mAudioDecodeHandler->GetFrame(buffer);
}

void AudioRenderHandler::NotifyListener(int what, int arg1, int arg2) {
    if (mNotifyCb) {
        mNotifyCb(what, arg1, arg2, nullptr, 0);
    }
}

void AudioRenderHandler::SetPlaybackRate(const float rate) {
    if (std::abs(rate - 0.0) > FLT_EPSILON &&
        std::abs(mPlaybackRate - rate) > FLT_EPSILON) {
        mPlaybackRate = rate;
    }
}

void AudioRenderHandler::SetVolume(const float leftVolume, const float rightVolume) {
    if (std::abs(mLeftVolume - leftVolume) < FLT_EPSILON &&
        std::abs(mRightVolume - rightVolume) < FLT_EPSILON) {
        return;
    }
    mLeftVolume    = std::min(std::max(leftVolume, 0.0f), 1.0f);
    mRightVolume   = std::min(std::max(rightVolume, 0.0f), 1.0f);
    bVolumeChanged = true;
}

void AudioRenderHandler::SetMute(bool mute) {
    mMute.store(mute);
}

int AudioRenderHandler::Flush() {
    std::unique_lock<std::mutex> lock(mLock);
    mCond.notify_one();
    return RESULT_OK;
}

int AudioRenderHandler::Stop() {
    std::unique_lock<std::mutex> lock(mLock);
    bAbort = true;
    mCond.notify_all();
    return RESULT_OK;
}

void AudioRenderHandler::Release() {
    NEXT_LOGD(TAG, "Release begin\n");
    int streamIndex = mMetaData->audio_index;
    for (auto & it : mMetaData->track_info) {
        if (it.stream_type == AVMEDIA_TYPE_AUDIO && it.stream_index == streamIndex
            && mDesired.channel_layout) {
            av_free(mDesired.channel_layout);
            break;
        }
    }
    if (mAudioRender) {
        mAudioRender->CloseAudio(true);
    }
    NEXT_LOGD(TAG, "Release end\n");
}
