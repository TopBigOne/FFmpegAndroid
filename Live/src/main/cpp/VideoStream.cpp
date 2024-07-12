
#include <cstring>
#include <jni.h>
#include "VideoStream.h"
#include "PushInterface.h"


void VideoStreamLogCallback(int level, const char *fmt, va_list args) {
    LOGD(__func__)
 //   LOGI(fmt, args)

    __android_log_vprint(ANDROID_LOG_INFO, "RTMP_LOG2 : ", fmt, args);
}


VideoStream::VideoStream() : m_frameLen(0),
                             videoCodec(nullptr),
                             pic_in(nullptr),
                             videoCallback(nullptr) {
    pthread_mutex_init(&mutex, nullptr);

    RTMP_LogSetCallback(VideoStreamLogCallback);
}

VideoStream::~VideoStream() {
    pthread_mutex_destroy(&mutex);
    if (videoCodec) {
        x264_encoder_close(videoCodec);
        videoCodec = nullptr;
    }
    if (pic_in) {
        x264_picture_clean(pic_in);
        DELETE(pic_in);
    }
}

void VideoStream::setVideoEncInfo(int width, int height, int fps, int bitrate) {
    LOGD(__FUNCTION__)
    LOGI("  width   : %d", width)
    LOGI("  height  :%d", height)
    LOGI("  fps     : %d", fps)
    LOGI("  bitrate : %d", bitrate)
    pthread_mutex_lock(&mutex);
    m_frameLen = width * height;
    if (videoCodec) {
        x264_encoder_close(videoCodec);
        videoCodec = nullptr;
    }
    if (pic_in) {
        x264_picture_clean(pic_in);
        DELETE(pic_in);
    }

    //setting x264 params
    // 设置编码器参数
    x264_param_t param;
    // ultrafast" 是预设参数的名称，用于指定编码速度和质量的平衡。在这个例子中，选择了 "ultrafast" 预设，它以非常快的速度进行编码，但可能会牺牲一定的视频质量。
    // "zerolatency" 是另一个预设参数的名称，用于指定编码器的延迟特性。在这个例子中，选择了 "zerolatency" 预设，它追求低延迟的编码，适用于实时视频传输等需要快速响应的场景。
    x264_param_default_preset(&param, "ultrafast", "zerolatency");
    param.i_level_idc          = 32;
    //input format
    param.i_csp                = X264_CSP_I420;
    param.i_width              = width;
    param.i_height             = height;
    //no B frame
    param.i_bframe             = 0;
    //i_rc_method:bitrate control, CQP(constant quality), CRF(constant bitrate), ABR(average bitrate)
    // 选择比特率控制方法为 X264_RC_ABR
    param.rc.i_rc_method       = X264_RC_ABR;
    //bitrate(Kbps)  设置比特率（单位：Kbps）
    param.rc.i_bitrate         = bitrate / 1000;
    //max bitrate  设置最大比特率
    param.rc.i_vbv_max_bitrate = bitrate / 1000 * 1.2;
    //unit:kbps  设置缓冲区大小（单位：Kbps）
    param.rc.i_vbv_buffer_size = bitrate / 1000;

    //frame rate  设置帧率
    param.i_fps_num        = fps;
    param.i_fps_den        = 1;
    param.i_timebase_den   = param.i_fps_num;
    param.i_timebase_num   = param.i_fps_den;
    //using fps   使用固定帧率
    param.b_vfr_input      = 0;
    //key frame interval(GOP)
    param.i_keyint_max     = fps * 2;
    //each key frame attaches sps/pps
    param.b_repeat_headers = 1;
    //thread number
    param.i_threads        = 1;

    x264_param_apply_profile(&param, "baseline");
    //open encoder
    videoCodec = x264_encoder_open(&param);
    pic_in     = new x264_picture_t;
    x264_picture_alloc(pic_in, X264_CSP_I420, width, height);
    pthread_mutex_unlock(&mutex);
}

void VideoStream::setVideoCallback(VideoCallback callback) {
    this->videoCallback = callback;
}

void VideoStream::encodeVideo(int8_t *data, int8_t camera_type) {
    pthread_mutex_lock(&mutex);

    if (camera_type == 1) {
        //  // 复制数据到图像的Y分量
        memcpy(pic_in->img.plane[0], data, m_frameLen); // y

        for (int i = 0; i < m_frameLen / 4; ++i) {
            // / 复制数据到图像的U分量
            *(pic_in->img.plane[1] + i) = *(data + m_frameLen + i * 2 + 1);  // u
            // // 复制数据到图像的V分量
            *(pic_in->img.plane[2] + i) = *(data + m_frameLen + i * 2); // v
        }
    } else if (camera_type == 2) {
        int offset = 0;
        //  // 复制数据到图像的Y分量
        memcpy(pic_in->img.plane[0], data, (size_t) m_frameLen); // y

        offset += m_frameLen;
        memcpy(pic_in->img.plane[1], data + offset, (size_t) m_frameLen / 4); // u
        // // 复制数据到图像的U分量
        offset += m_frameLen / 4;

        // 复制数据到图像的V分量
        memcpy(pic_in->img.plane[2], data + offset, (size_t) m_frameLen / 4); // v

    } else {
        return;
    }

    x264_nal_t     *pp_nal;
    int            pi_nal;
    x264_picture_t pic_out;
    //  使用编码器对图像进行编码
    x264_encoder_encode(videoCodec, &pp_nal, &pi_nal, pic_in, &pic_out);
    int      pps_len, sps_len = 0;
    uint8_t  sps[100];
    uint8_t  pps[100];
    for (int i                = 0; i < pi_nal; ++i) {

        // case 1:   如果是SPS（序列参数集）NAL单元
        if (pp_nal[i].i_type == NAL_SPS) {
            sps_len = pp_nal[i].i_payload - 4;
            // 复制SPS数据
            memcpy(sps, pp_nal[i].p_payload + 4, static_cast<size_t>(sps_len));
        } else if (pp_nal[i].i_type == NAL_PPS) {
            // // 如果是PPS（图像参数集）NAL单元
            pps_len = pp_nal[i].i_payload - 4;
            memcpy(pps, pp_nal[i].p_payload + 4, static_cast<size_t>(pps_len));
            // 发送SPS和PPS数据
            sendSpsPps(sps, pps, sps_len, pps_len);
        } else {
            // 发送帧数据
            sendFrame(pp_nal[i].i_type, pp_nal[i].p_payload, pp_nal[i].i_payload);
        }
    }
    pthread_mutex_unlock(&mutex);
}

void VideoStream::sendSpsPps(uint8_t *sps, uint8_t *pps, int sps_len, int pps_len) {
    int  bodySize = 13 + sps_len + 3 + pps_len;
    auto *packet  = new RTMPPacket;
    RTMPPacket_Alloc(packet, bodySize);
    int i = 0;
    //start code
    packet->m_body[i++] = 0x17;
    //type
    packet->m_body[i++] = 0x00;
    packet->m_body[i++] = 0x00;
    packet->m_body[i++] = 0x00;
    packet->m_body[i++] = 0x00;

    //version
    packet->m_body[i++] = 0x01;
    packet->m_body[i++] = sps[1];
    packet->m_body[i++] = sps[2];
    packet->m_body[i++] = sps[3];
    packet->m_body[i++] = 0xFF;

    //sps
    packet->m_body[i++] = 0xE1;
    //sps len
    packet->m_body[i++] = (sps_len >> 8) & 0xff;
    packet->m_body[i++] = sps_len & 0xff;
    memcpy(&packet->m_body[i], sps, sps_len);
    i += sps_len;

    //pps
    packet->m_body[i++] = 0x01;
    packet->m_body[i++] = (pps_len >> 8) & 0xff;
    packet->m_body[i++] = (pps_len) & 0xff;
    memcpy(&packet->m_body[i], pps, pps_len);

    //video
    packet->m_packetType      = RTMP_PACKET_TYPE_VIDEO;
    packet->m_nBodySize       = bodySize;
    packet->m_nChannel        = 10;
    //sps and pps no timestamp
    packet->m_nTimeStamp      = 0;
    packet->m_hasAbsTimestamp = 0;
    packet->m_headerType      = RTMP_PACKET_SIZE_MEDIUM;

    videoCallback(packet);
}

void VideoStream::sendFrame(int type, uint8_t *payload, int i_payload) {
    if (payload[2] == 0x00) {
        i_payload -= 4;
        payload += 4;
    } else {
        i_payload -= 3;
        payload += 3;
    }
    int  bodySize = 9 + i_payload;
    auto *packet  = new RTMPPacket;
    RTMPPacket_Alloc(packet, bodySize);

    packet->m_body[0] = 0x27;
    if (type == NAL_SLICE_IDR) {
        packet->m_body[0] = 0x17;
    }
    //packet type
    packet->m_body[1] = 0x01;
    //timestamp
    packet->m_body[2] = 0x00;
    packet->m_body[3] = 0x00;
    packet->m_body[4] = 0x00;
    //packet len
    packet->m_body[5] = (i_payload >> 24) & 0xff;
    packet->m_body[6] = (i_payload >> 16) & 0xff;
    packet->m_body[7] = (i_payload >> 8) & 0xff;
    packet->m_body[8] = (i_payload) & 0xff;

    memcpy(&packet->m_body[9], payload, static_cast<size_t>(i_payload));

    packet->m_hasAbsTimestamp = 0;
    packet->m_nBodySize       = bodySize;
    packet->m_packetType      = RTMP_PACKET_TYPE_VIDEO;
    packet->m_nChannel        = 0x10;
    packet->m_headerType      = RTMP_PACKET_SIZE_LARGE;
    videoCallback(packet);
}
