
#include <cstring>          // 引入 memcpy 等 C 字符串/内存操作函数
#include "VideoStream.h"    // 引入 VideoStream 类声明
#include "PushInterface.h"  // 引入 RTMP 推流接口（RTMPPacket 相关定义）

// 构造函数：初始化成员变量
// m_frameLen  : 单帧 YUV 亮度平面字节数（width * height），初始为 0
// videoCodec  : x264 编码器句柄，初始为空
// pic_in      : x264 输入图像结构体指针，初始为空
// videoCallback: 编码数据回调函数，初始为空
VideoStream::VideoStream():m_frameLen(0),
                           videoCodec(nullptr),
                           pic_in(nullptr),
                           videoCallback(nullptr) {

}

// 设置视频编码参数并初始化 x264 编码器
// 参数: width/height - 视频分辨率；fps - 帧率；bitrate - 目标码率(bps)
// 返回值: 0 表示成功，负数表示失败
int VideoStream::setVideoEncInfo(int width, int height, int fps, int bitrate) {
    std::lock_guard<std::mutex> l(m_mutex); // 加锁，防止多线程并发修改编码器状态
    m_frameLen = width * height;            // 计算亮度平面（Y 分量）的字节数
    if (videoCodec) {                       // 若已存在编码器实例，先关闭释放
        x264_encoder_close(videoCodec);     // 关闭 x264 编码器，释放内部资源
        videoCodec = nullptr;               // 置空句柄，避免野指针
    }
    if (pic_in) {                           // 若已分配输入图像内存，先释放
        x264_picture_clean(pic_in);         // 释放 x264_picture_t 内部分配的图像缓冲区
        delete pic_in;                      // 释放 pic_in 对象本身
        pic_in = nullptr;                   // 置空指针，避免野指针
    }

    //setting x264 params
    x264_param_t param;                                                     // 声明 x264 参数结构体
    int ret = x264_param_default_preset(&param, "ultrafast", "zerolatency");// 使用 ultrafast 预设（最低延迟编码速度）和 zerolatency 调优（零延迟，适合实时推流）
    if (ret < 0) {                                                          // 若预设加载失败
        return ret;                                                         // 直接返回错误码
    }
    param.i_level_idc = 32;         // 设置 H.264 Level 为 3.2（对应分辨率/码率上限）
    //input format
    param.i_csp = X264_CSP_I420;    // 设置输入色彩空间为 YUV 4:2:0 平面格式
    param.i_width = width;          // 设置编码宽度
    param.i_height = height;        // 设置编码高度
    //no B frame
    param.i_bframe = 0;             // 禁用 B 帧，降低编码延迟（直播推流必须）
    //i_rc_method:bitrate control, CQP(constant quality), CRF(constant bitrate), ABR(average bitrate)
    param.rc.i_rc_method = X264_RC_ABR;             // 码率控制模式：ABR（平均码率）
    //bitrate(Kbps)
    param.rc.i_bitrate = bitrate / 1024;            // 设置目标码率，单位 Kbps（bps 转换）
    //max bitrate
    param.rc.i_vbv_max_bitrate = bitrate / 1024 * 1.2; // 设置 VBV 最大码率为目标码率的 1.2 倍，允许短时突发
    //unit:kbps
    param.rc.i_vbv_buffer_size = bitrate / 1024;   // 设置 VBV 缓冲区大小，单位 Kbps，控制码率波动范围

    //frame rate
    param.i_fps_num = fps;                      // 帧率分子（即帧率值，如 30）
    param.i_fps_den = 1;                        // 帧率分母，固定为 1（帧率 = fps_num / fps_den）
    param.i_timebase_den = param.i_fps_num;     // 时间基分母设为帧率，使时间戳单位为 1/fps 秒
    param.i_timebase_num = param.i_fps_den;     // 时间基分子设为 1
    //using fps
    param.b_vfr_input = 0;                      // 禁用可变帧率输入，使用固定帧率（CFR）
    //key frame interval(GOP)
    param.i_keyint_max = fps * 2;               // 最大关键帧间隔（GOP 大小）= 2 秒内的帧数
    //each key frame attaches sps/pps
    param.b_repeat_headers = 1;                 // 每个关键帧前重复写入 SPS/PPS，方便播放端随时接入
    //thread number
    param.i_threads = 1;                        // 编码线程数设为 1，降低延迟（多线程会引入帧级延迟）

    ret = x264_param_apply_profile(&param, "baseline"); // 应用 baseline profile（兼容性最好，不含 B 帧/CABAC）
    if (ret < 0) {                                      // 若 profile 应用失败
        return ret;                                     // 返回错误码
    }
    //open encoder
    videoCodec = x264_encoder_open(&param); // 使用配置好的参数打开 x264 编码器，返回编码器句柄
    if (!videoCodec) {                      // 若编码器打开失败
        return -1;                          // 返回 -1 表示错误
    }
    pic_in = new x264_picture_t();                          // 动态分配 x264 输入图像结构体
    x264_picture_alloc(pic_in, X264_CSP_I420, width, height);// 按 I420 格式、指定分辨率分配图像内部缓冲区
    return ret;                                             // 返回 0 表示初始化成功
}

// 设置视频编码数据回调函数
// 编码完成后将通过该回调把 RTMPPacket 传递给推流层
void VideoStream::setVideoCallback(VideoCallback callback) {
    this->videoCallback = callback; // 保存回调函数指针
}

// 将 SPS 和 PPS 封装为 RTMP 视频序列头包并通过回调发送
// SPS/PPS 是解码器初始化必须的参数集，需在视频帧之前发送一次
// 参数: sps/pps - SPS/PPS 数据指针；sps_len/pps_len - 对应长度
void VideoStream::sendSpsPps(uint8_t *sps, uint8_t *pps, int sps_len, int pps_len) {
    int bodySize = 13 + sps_len + 3 + pps_len; // 计算包体总字节数：固定头13字节 + SPS数据 + PPS固定头3字节 + PPS数据
    auto *packet = new RTMPPacket();            // 动态分配 RTMP 数据包对象
    RTMPPacket_Alloc(packet, bodySize);         // 为包体分配 bodySize 字节的内存
    int i = 0;                                  // 包体写入偏移量
    // type
    packet->m_body[i++] = 0x17; // 帧类型(高4位 0x1=关键帧) + 编解码器ID(低4位 0x7=AVC/H.264)
    packet->m_body[i++] = 0x00; // AVC packet type：0x00 表示 AVC sequence header（序列头）
    // timestamp
    packet->m_body[i++] = 0x00; // Composition time offset 高字节（序列头固定为 0）
    packet->m_body[i++] = 0x00; // Composition time offset 中字节
    packet->m_body[i++] = 0x00; // Composition time offset 低字节

    //version
    packet->m_body[i++] = 0x01; // AVCDecoderConfigurationRecord 版本号，固定为 1
    // profile
    packet->m_body[i++] = sps[1]; // profile_idc：从 SPS 第2字节取，标识 H.264 profile（如 Baseline=66）
    packet->m_body[i++] = sps[2]; // profile_compatibility：从 SPS 第3字节取
    packet->m_body[i++] = sps[3]; // level_idc：从 SPS 第4字节取，标识 H.264 level
    packet->m_body[i++] = 0xFF;   // lengthSizeMinusOne(高6位固定1) + NALUnitLength=4字节(低2位=0x3)，0xFF=4字节长度前缀

    //sps
    packet->m_body[i++] = 0xE1;                    // numSequenceParameterSets(高3位固定0x7) + SPS数量(低5位=1)，0xE1=1个SPS
    //sps len
    // Note ??? 为啥不能直接写成： packet->m_body[i++] = sps_len ;
    /*
     *  如果直接写 = sps_len 会怎样？
  packet->m_body[i++] = sps_len;  // sps_len=300，但每格只有8bit
                                   // 300 超出1字节范围（最大255）
                                   // 编译器直接截断，只保留低字节=44
                                   // 高字节=1 丢失！

  接收方读到的长度就是 44 而不是 300，数据损坏。

  ---
  两个操作的含义

  sps_len >> 8 — 右移8位，把高字节移到低位：
  300 = 0000 0001 0010 1100
  >>8   0000 0000 0000 0001  =  1（高字节）

  & 0xFF — 只保留最低8位，其余清零：
  300 = 0000 0001 0010 1100
  &0xFF 0000 0000 1111 1111
      = 0000 0000 0010 1100  =  44（低字节）

  ---
  接收方怎么还原？

  int sps_len = (高字节 << 8) | 低字节
              = (1 << 8) | 44
              = 256 + 44
              = 300  ✓

  这就是大端序：高字节先传，低字节后传。
     * */
    packet->m_body[i++] = (sps_len >> 8) & 0xFF;   // SPS 长度高字节（大端序）
    packet->m_body[i++] = sps_len & 0xFF;           // SPS 长度低字节
    packet->m_body[i++] = sps_len ;
    memcpy(&packet->m_body[i], sps, sps_len);       // 将 SPS 数据拷贝到包体
    i += sps_len;                                   // 偏移量跳过已写入的 SPS 数据

    //pps
    packet->m_body[i++] = 0x01;                     // numPictureParameterSets：PPS 数量固定为 1
    packet->m_body[i++] = (pps_len >> 8) & 0xFF;    // PPS 长度高字节（大端序）
    packet->m_body[i++] = (pps_len) & 0xFF;         // PPS 长度低字节
    memcpy(&packet->m_body[i], pps, pps_len);        // 将 PPS 数据拷贝到包体

    //video
    packet->m_packetType = RTMP_PACKET_TYPE_VIDEO;  // 设置 RTMP 包类型为视频（0x09）
    packet->m_nBodySize  = bodySize;                // 设置包体实际长度
    packet->m_nChannel   = 0x10;                   // 设置 RTMP chunk stream ID（视频通道）
    //sps and pps no timestamp
    packet->m_nTimeStamp = 0;           // 序列头时间戳固定为 0
    packet->m_hasAbsTimestamp = 0;      // 使用相对时间戳（非绝对时间戳）
    packet->m_headerType = RTMP_PACKET_SIZE_MEDIUM; // 使用中等大小包头（8字节）

    videoCallback(packet); // 通过回调将封装好的 SPS/PPS 序列头包交给推流层发送
}

// 将一个 H.264 NALU 封装为 RTMP 视频数据包并通过回调发送
// 参数: type - NAL 单元类型（用于判断是否为关键帧）；payload - NALU 数据；i_payload - 数据长度
void VideoStream::sendFrame(int type, uint8_t *payload, int i_payload) {
    if (payload[2] == 0x00) {   // 判断起始码长度：payload[0..2]=0x00 0x00 0x00 说明是 4 字节起始码（0x00 0x00 0x00 0x01）
        i_payload -= 4;         // 去掉 4 字节起始码后的有效数据长度
        payload += 4;           // 跳过 4 字节起始码，指向 NALU 数据起始位置
    } else {                    // 否则是 3 字节起始码（0x00 0x00 0x01）
        i_payload -= 3;         // 去掉 3 字节起始码后的有效数据长度
        payload += 3;           // 跳过 3 字节起始码，指向 NALU 数据起始位置
    }
    int i = 0;                          // 包体写入偏移量
    int bodySize = 9 + i_payload;       // 包体总长度 = 9字节固定头 + NALU 有效数据长度
    auto *packet = new RTMPPacket();    // 动态分配 RTMP 数据包
    RTMPPacket_Alloc(packet, bodySize); // 为包体分配内存

    if (type == NAL_SLICE_IDR) {
        packet->m_body[i++] = 0x17; // IDR 关键帧：帧类型(高4位 0x1=关键帧) + 编解码器ID(低4位 0x7=AVC) // 1:Key frame  7:AVC
    } else {
        packet->m_body[i++] = 0x27; // 非关键帧：帧类型(高4位 0x2=非关键帧) + 编解码器ID(低4位 0x7=AVC) // 2:None key frame 7:AVC
    }
    //AVC NALU
    packet->m_body[i++] = 0x01; // AVC packet type：0x01 表示 AVC NALU（普通视频帧数据）
    //timestamp
    packet->m_body[i++] = 0x00; // Composition time offset 高字节（非 B 帧时固定为 0）
    packet->m_body[i++] = 0x00; // Composition time offset 中字节
    packet->m_body[i++] = 0x00; // Composition time offset 低字节
    //packet len
    packet->m_body[i++] = (i_payload >> 24) & 0xFF; // NALU 长度第1字节（最高位），大端序4字节长度前缀
    packet->m_body[i++] = (i_payload >> 16) & 0xFF; // NALU 长度第2字节
    packet->m_body[i++] = (i_payload >> 8) & 0xFF;  // NALU 长度第3字节
    packet->m_body[i++] = (i_payload) & 0xFF;        // NALU 长度第4字节（最低位）

    memcpy(&packet->m_body[i], payload, static_cast<size_t>(i_payload)); // 将 NALU 有效数据拷贝到包体

    packet->m_hasAbsTimestamp = 0;                  // 使用相对时间戳
    packet->m_nBodySize       = bodySize;           // 设置包体实际大小
    packet->m_packetType      = RTMP_PACKET_TYPE_VIDEO; // 设置包类型为视频
    packet->m_nChannel        = 0x10;              // 设置 RTMP chunk stream ID（视频通道）
    packet->m_headerType      = RTMP_PACKET_SIZE_LARGE; // 使用大包头（12字节，包含完整时间戳）
    videoCallback(packet); // 通过回调将封装好的视频帧包交给推流层发送
}

// 对一帧原始 YUV 数据进行 H.264 编码，并将编码结果通过回调发送
// 参数: data - 原始图像数据（NV21 或 I420 格式）；camera_type - 相机数据格式类型
//       camera_type == 1: NV21 格式（Android 相机默认输出，Y + VU 交错）
//       camera_type == 2: I420 格式（标准 YUV 4:2:0 平面格式，Y + U + V 分离）
void VideoStream::encodeVideo(int8_t *data, int camera_type) {
    std::lock_guard<std::mutex> l(m_mutex); // 加锁，确保编码过程线程安全
    if (!pic_in)    // 若编码器尚未初始化（pic_in 为空）
        return;     // 直接返回，不进行编码

    if (camera_type == 1) {                                             // NV21 格式：Y 平面 + VU 交错平面
        memcpy(pic_in->img.plane[0], data, m_frameLen);                 // 拷贝 Y（亮度）分量，大小为 width*height 字节
        for (int i = 0; i < m_frameLen/4; ++i) {                       // 遍历每个 UV 像素对（总数为 Y 像素数的 1/4）
            *(pic_in->img.plane[1] + i) = *(data + m_frameLen + i * 2 + 1);  // 从 NV21 的 VU 交错数据中提取 U 分量（奇数偏移为 U）
            *(pic_in->img.plane[2] + i) = *(data + m_frameLen + i * 2); // 从 NV21 的 VU 交错数据中提取 V 分量（偶数偏移为 V）
        }
    } else if (camera_type == 2) {                                      // I420 格式：Y/U/V 三平面分离
        int offset = 0;                                                 // 数据读取偏移量
        memcpy(pic_in->img.plane[0], data, (size_t) m_frameLen);        // 拷贝 Y 分量，大小为 width*height
        offset += m_frameLen;                                           // 偏移跳过 Y 平面
        memcpy(pic_in->img.plane[1], data + offset, (size_t) m_frameLen / 4); // 拷贝 U 分量，大小为 Y 的 1/4
        offset += m_frameLen / 4;                                       // 偏移跳过 U 平面
        memcpy(pic_in->img.plane[2], data + offset, (size_t) m_frameLen / 4); // 拷贝 V 分量，大小为 Y 的 1/4
    } else {    // 不支持的相机格式
        return; // 直接返回，不进行编码
    }

    x264_nal_t *pp_nal;         // 指向编码输出的 NAL 单元数组
    int pi_nal;                  // 编码输出的 NAL 单元数量
    x264_picture_t pic_out;      // 编码输出图像信息（包含 PTS/DTS 等）
    x264_encoder_encode(videoCodec, &pp_nal, &pi_nal, pic_in, &pic_out); // 执行 x264 编码，将 pic_in 编码为 NAL 单元列表
    int pps_len, sps_len = 0;   // PPS/SPS 数据长度（sps_len 初始化为 0）
    uint8_t sps[100];            // SPS 数据缓冲区（最大 100 字节）
    uint8_t pps[100];            // PPS 数据缓冲区（最大 100 字节）
    for (int i = 0; i < pi_nal; ++i) {      // 遍历所有编码输出的 NAL 单元
        x264_nal_t nal = pp_nal[i];         // 取出第 i 个 NAL 单元
        if (nal.i_type == NAL_SPS) {                                        // 若当前 NAL 为 SPS（序列参数集）
            sps_len = nal.i_payload - 4;                                    // 去掉 4 字节起始码后的 SPS 实际长度
            memcpy(sps, nal.p_payload + 4, static_cast<size_t>(sps_len));   // 跳过起始码，拷贝 SPS 数据到缓冲区
        } else if (nal.i_type == NAL_PPS) {                                 // 若当前 NAL 为 PPS（图像参数集）
            pps_len = nal.i_payload - 4;                                    // 去掉 4 字节起始码后的 PPS 实际长度
            memcpy(pps, nal.p_payload + 4, static_cast<size_t>(pps_len));   // 跳过起始码，拷贝 PPS 数据到缓冲区
            sendSpsPps(sps, pps, sps_len, pps_len);                         // SPS 和 PPS 都已就绪，封装并发送序列头包
        } else {                                                            // 其他类型 NAL（IDR 帧、P 帧等）
            sendFrame(nal.i_type, nal.p_payload, nal.i_payload);            // 封装并发送视频帧数据包
        }
    }
}

// 析构函数：释放编码器和图像缓冲区资源
VideoStream::~VideoStream() {
    if (videoCodec) {               // 若编码器句柄有效
        x264_encoder_close(videoCodec); // 关闭 x264 编码器，释放内部资源
        videoCodec = nullptr;           // 置空指针，防止悬空引用
    }
    if (pic_in) {                   // 若输入图像结构体有效
        x264_picture_clean(pic_in); // 释放 pic_in 内部分配的图像平面缓冲区
        delete pic_in;              // 释放 pic_in 对象本身
        pic_in = nullptr;           // 置空指针，防止悬空引用
    }
}
