#ifndef AUDIO_RENDER_HANDLER_H
#define AUDIO_RENDER_HANDLER_H

#include <condition_variable>
#include <mutex>
#include <string>

#include "common/NextFrameBuffer.h"
#include "common/NextFrameQueue.h"
#include "common/MediaClock.h"
#include "kernel/handler/AudioDecodeHandler.h"
#include "render/AudioRenderFactory.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "libavutil/opt.h"
#include "libswresample/swresample.h"
#ifdef __cplusplus
}
#endif

typedef void (*AudioRenderAudioCallback)(void *opaque, char *stream, int &len);

class AudioCallbackClass : public AudioCallback {
public:
    AudioCallbackClass(void *userData, AudioRenderAudioCallback callback)
            : AudioCallback(userData) {
        mCallback = callback;
    }

    ~AudioCallbackClass() override = default;

    void GetBuffer(uint8_t *stream, int len) override {
        if (mCallback) {
            mCallback(mUserData, reinterpret_cast<char *>(stream), len);
        }
    };

private:
    AudioRenderAudioCallback mCallback;
};

class AudioRenderHandler : public BaseThread {
public:
    AudioRenderHandler(sp<AudioDecodeHandler> &audioDecodeHandler,
                       const sp<PlayerLink> &pLink,
                       NotifyCallback notifyCb,
                       const char *threadName);

    ~AudioRenderHandler() override;

    void SetConfig(const sp<GeneralConfig> &config);

    int Prepare(sp<MetaData> &metadata);

    int StartRender();

    int PauseRender();

    void ExecuteTask() override;

    void SetPlaybackRate(float rate);

    void SetVolume(float leftVolume, float rightVolume);

    void SetMute(bool mute);

    int Flush();

    int Stop();

    void Release();

private:

    int Init();

    int ReadFrame(std::unique_ptr<FrameBuffer> &buffer);

    static void AudioDataCallback(void *opaque, char *data, int &len);

    void GetAudioData(char *data, int &len);

    void NotifyListener(int what, int arg1 = 0, int arg2 = 0);

    int ResampleAudioData(std::unique_ptr<FrameBuffer> &buffer);

private:

    int mLastReadPos{0};
    int mBytesPerSec{0};
    int mAudioBufSize{0};

    unsigned int mAudioBuf1Size{0};

    bool bAbort{false};
    bool bPaused{false};
    bool bVolumeChanged{false};
    bool bFirstFrameDecoded{false};

    float mLeftVolume{1.0};
    float mRightVolume{1.0};
    float mPlaybackRate{1.0};
    double mAudioDelay{0.0};

    uint8_t *mAudioBuf{nullptr};
    uint8_t *mAudioBuf1{nullptr};
    uint8_t *mAudioNewBuf{nullptr};

    sp<MetaData> mMetaData;
    sp<PlayerLink> mPlayerLink;
    sp<GeneralConfig> mGeneralConfig;
    sp<AudioDecodeHandler> mAudioDecodeHandler;

    std::mutex mLock;
    NotifyCallback mNotifyCb;
    std::atomic_bool mMute{false};
    std::condition_variable mCond;
    struct SwrContext *mSwrContext{nullptr};

    AudioRenderInfo mDesired{};
    AudioRenderInfo mObtained{};
    AudioRenderInfo mCurrentInfo{};
    std::unique_ptr<FrameBuffer>   mFrameBuffer{nullptr};
    std::unique_ptr<AudioRender>   mAudioRender{nullptr};
    std::unique_ptr<AudioCallback> mAudioCallback{nullptr};

};

#endif //AUDIO_RENDER_HANDLER_H
