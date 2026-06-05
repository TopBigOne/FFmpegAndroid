#ifndef VIDEO_DECODE_HANDLER_H
#define VIDEO_DECODE_HANDLER_H

#include <condition_variable>
#include <mutex>
#include <queue>

#if defined(__ANDROID__)
#include <android/native_window.h>
#endif

#include "common/BaseThread.h"
#include "common/NextFrameQueue.h"
#include "common/NextPacket.h"
#include "common/NextSpeedMeter.h"
#include "decode/VideoDecoder.h"
#include "MediaParseHandler.h"
#include "NextErrorCode.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "libavcodec/avcodec.h"
#include "libavutil/frame.h"
#ifdef __cplusplus
}
#endif

class VideoDecodeHandler : public BaseThread, VideoDecodeCallback {
public:
    VideoDecodeHandler(sp<MediaParseHandler> &mediaParser,
                       const sp<PlayerLink> &pLink, NotifyCallback notifyCb,
                       const char *threadName);

    ~VideoDecodeHandler() override;

    void SetConfig(const sp<GeneralConfig> &config);

    int Init(sp<MetaData> &metadata);

    int GetFrame(std::unique_ptr<FrameBuffer> &buffer);

#if defined(__ANDROID__)

    int SetNativeSurface(ANativeWindow *surface);

#endif

    void ExecuteTask() override;

    int GetSerial();

    void ResetEof();

    int GetQueueSize();

    int OnDecodedFrame(std::unique_ptr<MixedBuffer> frame) override;

    void OnDecodeError(int error, int errorCode) override;

    int Stop();

    void Release();

private:
    VideoDecodeHandler() = default;

    int InitInternal();

    int PerformDecode(AVPacket *pkt);

    int ReadPacketOrBuffering(std::unique_ptr<NextPacket> &pkt);

    int PerformFlush();

    int ResetDecoder();

    int ResetDecoderFormat();

    void DecodeLastCacheGop();

    void NotifyListener(int what, int arg1 = 0, int arg2 = 0);

    bool FrontIsFlush();

private:

    int mWidth{0};
    int mHeight{0};
    int mLastSerial{-1};
    int mInputPacketCount{0};
    int mDecodeErrorCount{0};

    bool bEOF{false};
    bool bIdrIdentified{true};
    bool bRefreshSession{false};
    bool bDecoderRecovery{false};
    bool bFirstFrameDecoded{false};
    bool bFirstPacketReceived{false};
    std::atomic_bool bReleased{false};
    std::atomic_bool mSurfaceUpdated{false};

    sp<MetaData>   mMetaData;
    sp<FrameQueue> mFrameQueue;
    sp<PlayerLink> mPlayerLink;
    sp<GeneralConfig>     mGeneralConfig;
    sp<MediaParseHandler> mMediaParser;
    std::unique_ptr<MixedBuffer> mBuffer;
    std::unique_ptr<NextPacket>  mPendingPkt;
    std::unique_ptr<VideoDecoder> mVideoDecoder;

    std::mutex mLock;
    NotifyCallback mNotifyCb;
    VideoSpeedMeter mSpeedMeter;
    std::condition_variable mCond;
    std::queue<std::shared_ptr<NextPacket>> mPktQueue;

#if defined(__ANDROID__)
    ANativeWindow *mCurNativeWindow{};
#endif

};

#endif //VIDEO_DECODE_HANDLER_H
