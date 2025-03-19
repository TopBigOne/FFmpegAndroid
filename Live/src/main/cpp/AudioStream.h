
#ifndef AUDIOSTREAM_H
#define AUDIOSTREAM_H

#include "rtmp/log.h"
#include "rtmp/rtmp.h"
#include "faac/faac.h"
#include <sys/types.h>
#include "LogUtil.h"

class AudioStream {
    typedef void (*AudioCallback)(RTMPPacket *packet);

private:
    AudioCallback audioCallback;
    int mChannels;
    faacEncHandle audioCodec = 0;
    u_long inputSamples;
    u_long maxOutputBytes;
    u_char *buffer = 0;

public:
    AudioStream();

    ~AudioStream();

    int setAudioEncInfo(int samplesInHZ, int channels);

    void setAudioCallback(AudioCallback audioCallback);

    int getInputSamples() const;

    void encodeData(int8_t *data);

    RTMPPacket *getAudioTag();

};


#endif
