# FFmpegAndroid

### [FFmpeg官方文档](https://ffmpeg.org/)
### [FFmpeg编译流程](https://github.com/xufuji456/FFmpegAndroid/blob/master/doc/FFmpeg_compile_shell.md)
### [FFmpeg常用命令行](https://github.com/xufuji456/FFmpegAndroid/blob/master/doc/FFmpeg_command_line.md)
### [FFmpeg源码分析](https://github.com/xufuji456/FFmpegAndroid/blob/master/doc/FFmpeg_sourcecode.md)
### [JNI与NDK开发](https://github.com/xufuji456/FFmpegAndroid/blob/master/doc/JNI_develop_practice.md)
### [音视频知识汇总](https://github.com/xufuji456/FFmpegAndroid/blob/master/doc/multimedia_knowledge.md)
### [ijkplayer播放器架构](https://github.com/xufuji456/FFmpegAndroid/blob/master/doc/player_framework.md)

----------------------------------------------------------------------------------------------------

常见的流媒体传输协议包括：RTP、RTMP、RTCP、RTSP，流媒体应用协议有HLS、DASH，</br>
WebRTC设计传输协议有SDP、SRTP、ICE、NAT、STUN等，常用视频编码协议有H264，</br>
常用的视频封装格式有mp4，关于C/C++语言标准有C11、C20++等，书籍包括音视频编解码等。</br>
详细列表可以查阅：[多媒体协议与书籍](https://github.com/xufuji456/FFmpegAndroid/blob/master/doc/multimedia_protocol.md)

音视频工作方向包括:直播、短视频、流媒体传输、视频播放器、音乐播放器、音视频算法、</br>
流媒体后端、音视频编辑、图像处理(个人概括，具体方向不限于此)。</br>
详情可查阅：[音视频工作方向](https://github.com/xufuji456/FFmpegAndroid/blob/master/doc/multimedia_work.md)

### 音视频基础知识:
![preview](https://github.com/xufuji456/FFmpegAndroid/blob/master/picture/multimedia_baseline.png)

### 音视频进阶成长:
![preview](https://github.com/xufuji456/FFmpegAndroid/blob/master/picture/multimedia_main.png)

### 音视频开源库:
![preview](https://github.com/xufuji456/FFmpegAndroid/blob/master/picture/multimedia_library.png)

### Joining the group to learn FFmpeg:
![preview](https://github.com/xufuji456/FFmpegAndroid/blob/master/picture/ffmpeg_group.png)



--

rtmp://127.0.0.1:2020/live


--
* Android 推荐的 PreViewFormat 是 NV21，在 PreviewCallback 中会返回 Preview 的 N21 图片。如果是软编的话，由于 H264 支持 I420 的图片格式，因此需要将 N21格式转为 I420 格式，然后交给 x264 编码库。如果是硬编的话，
* 由于 Android 硬编编码器支持 I420(COLOR_FormatYUV420Planar) 和NV12(COLOR_FormatYUV420SemiPlanar)，因此可以将 N21 的图片转为 I420 或者 NV12 ，然后交给硬编编码器。

