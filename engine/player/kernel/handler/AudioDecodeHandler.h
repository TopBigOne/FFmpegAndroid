#ifndef AUDIO_DECODE_HANDLER_H
#define AUDIO_DECODE_HANDLER_H

#include <condition_variable>
#include <mutex>
#include <queue>

#include "common/NextFrameBuffer.h"
#include "common/NextFrameQueue.h"
#include "decode/AudioDecoder.h"
#include "MediaParseHandler.h"


class AudioDecodeHandler : public BaseThread, AudioDecodeCallback {
public:
    AudioDecodeHandler(sp<MediaParseHandler> &mediaParseHandler, const sp<PlayerLink> &pLink,
                       NotifyCallback notifyCb, const char *threadName);

    ~AudioDecodeHandler() override;

    void ExecuteTask() override;

    void SetConfig(const sp<GeneralConfig> &config);

    int Prepare(const sp<MetaData> &metadata);

    int GetFrame(std::unique_ptr<FrameBuffer> &buffer);

    int GetSerial();

    int OnDecodedFrame(AVFrame *frame) override;

    void OnDecodeError(int error) override;

    void ResetEof();

    int Stop();

    void Release();

private:

    int Init();

    int PerformDecode(AVPacket *pkt);

    int ReadPacketOrBuffering(std::unique_ptr<NextPacket> &pkt);

    int PerformFlush();

    int ResetDecoderFormat();

    void NotifyListener(int what, int arg1 = 0, int arg2 = 0);

    bool CheckAccurateSeek(const std::unique_ptr<FrameBuffer> &buffer);

private:

    bool bEOF{false};
    bool bReleased{false};

    std::mutex mLock;
    std::condition_variable mCond;
    std::unique_ptr<FrameQueue> mFrameQueue;
    std::unique_ptr<AudioDecoder> mAudioDecoder;

    sp<MetaData> mMetaData;
    sp<PlayerLink> mPlayerLink;
    sp<GeneralConfig> mGeneralConfig;
    sp<MediaParseHandler> mRedSourceController;

    NotifyCallback mNotifyCb;
};

#endif //AUDIO_DECODE_HANDLER_H