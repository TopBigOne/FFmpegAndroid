#!/bin/bash

# 获取本脚本所在的绝对目录，保证子路径引用不受调用目录影响
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# 加载公共配置：FFSRC、NDK_ROOT、SO_OUT 等路径变量
source "$SCRIPT_DIR/../my_config.sh"

# 切换到 FFmpeg 源码目录，configure/make 必须在源码根目录下执行
cd "$FFSRC"

# 清理上次编译产物，避免旧的 config.mak / .o 文件干扰本次 configure
make clean

# 开启错误即退出模式：后续任意命令失败则立即终止脚本
set -e

# 目标架构位数：64 = arm64-v8a，32 = armeabi-v7a
archbit=64

if [ $archbit -eq 32 ];then
  echo "build for 32bit"
  ARCH='arm'                    # binutils 前缀用的架构名
  CPU='armv7-a'                 # 传给 --cpu 的指令集
  ABI='armeabi-v7a'             # Android ABI 目录名
  API=21                        # 最低支持的 Android API 级别
  PLATFORM='armv7a'             # clang 可执行文件名中的平台字段
  PLATFORM_ARCH='arm'           # NDK platforms 目录中的架构名
  ANDROID='androideabi'         # 32 位 ARM 的 ABI 后缀
  OPTIMIZE_CFLAGS="-march=$CPU -mfpu=neon -mfloat-abi=softfp -marm"  # 32 位专属优化标志
else
  echo "build for 64bit"
  ARCH='aarch64'                # 64 位 ARM 架构名
  CPU='armv8-a'                 # ARMv8-A 指令集
  ABI='arm64-v8a'               # Android ABI 目录名
  API=21                        # 最低支持的 Android API 级别
  PLATFORM='aarch64'            # clang 可执行文件名中的平台字段
  PLATFORM_ARCH='arm64'         # NDK platforms 目录中的架构名
  ANDROID='android'             # 64 位 ARM 的 ABI 后缀
  OPTIMIZE_CFLAGS="-march=$CPU" # 64 位只需指定指令集，不需要 -mfpu/-mfloat-abi
fi

# 获取当前操作系统类型，用于拼接 NDK 预编译工具链的路径
uname=`uname`
if [ $uname = "Darwin" ];then
  COMPILE_OS="darwin"
  CORE_NUM=$(getconf _NPROCESSORS_ONLN)   # macOS 获取 CPU 核心数
  echo "compile on mac, core=$CORE_NUM"
elif [ $uname = "Linux" ]; then
  COMPILE_OS="linux"
  CORE_NUM=$(nproc)                        # Linux 获取 CPU 核心数
  echo "compile on linux, core=$CORE_NUM"
else
  echo "don't support $uname"
fi

# NDK 根目录（来自 my_config.sh 的 NDK_ROOT）
export NDK=$NDK_ROOT

# LLVM 工具链所在的顶层目录（包含 bin/ sysroot/ 等子目录）
export TOOL=$NDK/toolchains/llvm/prebuilt/$COMPILE_OS-x86_64

# 工具链 bin 目录，存放 clang、llvm-ar、llvm-nm 等可执行文件
export TOOLCHAIN=$TOOL/bin

# Android 系统头文件和系统库（libc、libm 等）所在的 sysroot
export SYSROOT=$TOOL/sysroot

# 交叉编译工具前缀，用于查找 ld、objcopy 等（NDK 28 已改为 llvm- 前缀）
export CROSS_PREFIX=$TOOLCHAIN/$ARCH-linux-$ANDROID-

# C 编译器：aarch64-linux-android21-clang（已内置 sysroot 和 target）
export CC=$TOOLCHAIN/$PLATFORM-linux-$ANDROID$API-clang

# C++ 编译器
export CXX=$TOOLCHAIN/$PLATFORM-linux-$ANDROID$API-clang++

# NDK 旧版 platforms 目录（NDK 28 已删除，此变量在新链接方式下不再使用）
export PLATFORM_API=$NDK/platforms/android-$API/arch-$PLATFORM_ARCH

# 最终产物输出目录，例如：so_out/arm64-v8a/
export PREFIX=$SO_OUT/$ABI

# 第三方库（openssl、mp3lame、x264）也安装在同一目录，复用 PREFIX
THIRD_LIB=$PREFIX

# 编译时额外的 C 标志：优化等级、位置无关代码、架构优化、头文件搜索路径
export EXTRA_CFLAGS="-Os -fPIC $OPTIMIZE_CFLAGS -I$THIRD_LIB/include"

# 链接时额外的标志：页大小对齐（Android 15+ 要求 16KB）、库搜索路径
export EXTRA_LDFLAGS="-Wl,-z,max-page-size=16384 -L$THIRD_LIB/lib"

# 告诉 pkg-config 去哪里找 .pc 文件（openssl.pc、x264.pc、libmp3lame.pc）
export PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig

# 覆盖 pkg-config 默认的系统路径，防止误匹配 Mac 本机的库（交叉编译必须设置）
export PKG_CONFIG_LIBDIR=$PREFIX/lib/pkgconfig

function build_one() {
  # configure 参数说明：
  # --target-os=android            目标操作系统
  # --prefix                       安装目录（头文件、静态库输出到这里）
  # --cross-prefix                 交叉编译工具前缀
  # --enable-cross-compile         启用交叉编译模式
  # --arch/--cpu                   目标 CPU 架构和指令集
  # --cc/--cxx                     C/C++ 编译器
  # --ar/--ranlib/--nm/--strip     NDK 28 用 llvm- 前缀工具替代旧的 arch-prefixed 工具
  # --sysroot                      Android 系统头文件和库的根目录
  # --enable-hwaccels              启用所有硬件加速（MediaCodec 等）
  # --enable-static/--disable-shared  编译为静态库，最终由 link_one_ffmpeg 合并成 .so
  # --disable-doc                  不生成文档，加快编译
  # --enable-neon/--enable-asm     启用 ARM NEON SIMD 指令集和汇编优化
  # --disable-small                不以减小体积为优先（保留性能优化）
  # --disable-ffmpeg/ffplay/ffprobe  不编译命令行工具
  # --disable-debug                关闭调试信息，减小体积
  # --enable-gpl                   启用 GPL 协议组件（x264 依赖此选项）
  # --pkg-config="pkg-config --static"  用 pkg-config 查找静态库依赖
  # --disable-avdevice/indevs/outdevs   禁用设备模块（Android 不需要）
  # --extra-cflags/--extra-ldflags      附加编译和链接标志
  # --enable-avcodec/avformat 等        各功能模块
  # --enable-network               启用网络功能（RTMP、HLS、HTTPS 播放需要）
  # --enable-bsfs                  启用比特流过滤器（H.264/HEVC 格式转换等）
  # --disable-postproc             禁用后处理模块（去块效应等，移动端一般不用）
  # --disable-encoders/decoders    先全禁再按需开启，减小 .so 体积
  # --enable-libmp3lame/libx264/libx265  启用第三方编码库（x265 需要 --enable-gpl）
  # --enable-libx265：告诉 FFmpeg configure 去找 libx265 这个外部库（通过 pkg-config 查 x265.pc），并编译对它的支持
  # --enable-encoder=...,libx265：在已找到外部库的前提下，开启 libx265 这个编码器
  # --enable-jni/--enable-mediacodec   Android JNI 和 MediaCodec 硬件加解码
  # --enable-nonfree               启用非自由协议组件（openssl 需要此选项）
  # --enable-openssl               启用 OpenSSL（提供 TLS/HTTPS 支持）
  # --disable-demuxers             先全禁再按需开启解封装器，减小体积
  ./configure \
  --target-os=android \
  --prefix=$PREFIX \
  --cross-prefix=$CROSS_PREFIX \
  --enable-cross-compile \
  --arch=$ARCH \
  --cpu=$CPU \
  --cc=$CC \
  --cxx=$CXX \
  --ar=$TOOLCHAIN/llvm-ar \
  --ranlib=$TOOLCHAIN/llvm-ranlib \
  --nm=$TOOLCHAIN/llvm-nm \
  --strip=$TOOLCHAIN/llvm-strip \
  --enable-cross-compile \
  --sysroot=$SYSROOT \
  --enable-hwaccels \
  --enable-static \
  --disable-shared \
  --disable-doc \
  --enable-neon \
  --enable-asm \
  --disable-small \
  --disable-ffmpeg \
  --disable-ffplay \
  --disable-ffprobe \
  --disable-debug \
  --enable-gpl \
  --pkg-config="pkg-config --static" \
  --disable-avdevice \
  --disable-indevs \
  --disable-outdevs \
  --extra-cflags="$EXTRA_CFLAGS" \
  --extra-ldflags="$EXTRA_LDFLAGS" \
  --enable-avcodec \
  --enable-avformat \
  --enable-avutil \
  --enable-swresample \
  --enable-swscale \
  --enable-avfilter \
  --enable-network \
  --enable-bsfs \
  --disable-postproc \
  --enable-filters \
  --disable-encoders \
  --enable-libmp3lame \
  --enable-libx264 \
  --enable-libx265 \
  --enable-encoder=libmp3lame,libx264,libx265 \
  --enable-encoder=apng,bmp,dvvideo,flv,gif,h263,h264,hdr,jpeg2000,ljpeg,mjpeg,\
mpeg1video,mpeg2video,mpeg4,msmpeg4v2,msmpeg4,png,prores,qtrle,rawvideo,tiff,\
wmv1,wmv2,xbm,zlib,aac,ac3,g722,g726,adpcm_ima_qt,adpcm_ima_wav,adpcm_ms,alac,\
eac3,flac,mp2,opus,pcm_alaw,pcm_mulaw,pcm_f32le,pcm_f64le,pcm_s16be,pcm_s16le,\
pcm_s32be,pcm_s32le,pcm_s64be,pcm_s64le,pcm_s8,pcm_u16le,pcm_u32le,pcm_u8,sonic,\
truehd,tta,vorbis,wavpack,wmav1,wmav2,ssa,ass,dvbsub,dvdsub,mov_text,srt,subrip,ttml,webvtt \
  --disable-decoders \
  --enable-decoder=av1,dvvideo,flv,gif,h263,h264,hevc,mjpeg,mpeg1video,mpeg2video,mpegvideo,mpeg4,\
msmpeg4v2,msmpeg4v3,msvideo1,png,tscc,tscc2,vc1,vp8,vp9,webp,wmv1,wmv2,wmv3,zlib,\
aac,aac_latm,ac3,adpcm_ima_qt,adpcm_ima_wav,adpcm_ms,alac,amrnb,amrwb,ape,dolby_e,\
eac3,flac,g722,g726,g729,m4a,mp3float,mp3,mp3adufloat,mp3adu,mp3on4,opus,pcm_alaw,\
pcm_mulaw,pcm_dvd,pcm_f16le,pcm_f24le,pcm_f32be,pcm_f32le,pcm_f64be,pcm_f64le,pcm_s16be,pcm_s16le,\
pcm_s24be,pcm_s24le,pcm_s32be,pcm_s32le,pcm_s64be,pcm_s64le,pcm_u16be,pcm_u16le,pcm_u24be,pcm_u24le,\
pcm_u32be,pcm_u32le,pcm_vidc,pcm_zork,truehd,truespeech,vorbis,wmav1,wmav2,\
ssa,ass,dvbsub,dvdsub,pgssub,mov_text,sami,srt,subrip,text,webvtt \
  --enable-jni \
  --enable-mediacodec \
  --enable-decoder=h264_mediacodec \
  --enable-decoder=hevc_mediacodec \
  --enable-decoder=mpeg4_mediacodec \
  --enable-decoder=vp9_mediacodec \
  --enable-muxers \
  --enable-parsers \
  --enable-nonfree \
  --enable-protocols \
  --enable-openssl \
  --enable-protocol=https \
  --disable-demuxers \
  --enable-demuxer=aac,ac3,alaw,amr,amrnb,amrwb,ape,asf,asf_o,ass,av1,avi,cavsvideo,codec2,concat,dash,dnxhd,eac3,flac,flv,\
g722,g726,g729,gif,gif_pipe,h263,h264,hevc,hls,image2,image2pipe,jpeg_pipe,lrc,m4v,matroska,webm,mjpeg,mov,mp4,m4a,3gp,mp3,mpeg,\
mpegts,mpegvideo,mv,mulaw,manifest,ogg,pcm_s16be,pcm_s16le,pcm_s32be,pcm_s32le,pcm_f32be,pcm_f32le,pcm_f64be,pcm_f64le,\
png_pipe,realtext,rm,rtp,rtsp,sami,sdp,srt,swf,vc1,wav,webm_dash,xmv

  # 多线程并行编译，-j 后面是 CPU 核心数
  make -j$CORE_NUM

  # 安装头文件和静态库到 $PREFIX 目录
  make install
}

# 执行编译和安装
echo ">>> [1/2] 开始编译 FFmpeg 静态库..."
build_one
echo ">>> [1/2] FFmpeg 静态库编译完成"

function link_one_ffmpeg() {
  # 用 clang 将所有静态库链接成一个 libffmpeg.so，比直接调用 ld 更安全：
  # clang 会自动处理 sysroot 和 runtime 库路径。
  # -Wl,--whole-archive：强制把后续所有 .a 的符号全部打入 .so，
  #   避免 FFmpeg 内部符号因未被外部引用而被 ld 丢弃。
  # -Wl,--no-whole-archive：关闭 whole-archive 模式，后续系统库正常按需链接。
  # -Wl,-Bsymbolic：.so 内部的全局符号引用在链接时直接解析（不走 GOT）。
  #   FFmpeg 的 NEON 汇编（tx_float_neon.S 等）用 ADRP 直接寻址全局数据表，
  #   lld 对默认可见性符号要求 GOT 寻址，加此选项后 ADRP 就合法了。
  # -Wl,-z,max-page-size=16384：Android 15+ 强制要求 16KB 页对齐。
  # -lc -lm -lz -ldl：C 库、数学、zlib 压缩、动态链接。
  # -llog：Android 日志（__android_log_print）。
  # -landroid：Android Native API。
  # -lmediandk：Android MediaCodec NDK 接口。
  # -lc++_static：x265 是 C++ 库，需要静态链接 NDK C++ 运行时，避免运行时依赖 libc++_shared.so。
  $CC \
  -shared \
  -Wl,-soname,libffmpeg.so \
  -Wl,--whole-archive \
  $PREFIX/lib/libavcodec.a \
  $PREFIX/lib/libavfilter.a \
  $PREFIX/lib/libswresample.a \
  $PREFIX/lib/libavformat.a \
  $PREFIX/lib/libavutil.a \
  $PREFIX/lib/libswscale.a \
  $PREFIX/lib/libmp3lame.a \
  $PREFIX/lib/libx264.a \
  $PREFIX/lib/libx265.a \
  $PREFIX/lib/libssl.a \
  $PREFIX/lib/libcrypto.a \
  -Wl,--no-whole-archive \
  -Wl,-Bsymbolic \
  -Wl,-z,max-page-size=16384 \
  -lc -lm -lz -ldl \
  -llog \
  -landroid \
  -lmediandk \
  -lc++_static \
  -o $PREFIX/libffmpeg.so
}

# 执行链接，生成最终的 libffmpeg.so
echo ">>> [2/2] 开始链接 libffmpeg.so..."
link_one_ffmpeg
echo "✅✅✅build ffmpeg done, output: $PREFIX"
