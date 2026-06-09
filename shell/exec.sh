#!/bin/bash

# 获取本脚本所在的绝对目录路径
# 这样无论从哪个目录执行这个脚本，子脚本的路径都能正确解析
# 例如：在项目根目录执行 bash shell/exec.sh，SCRIPT_DIR = /path/to/project/shell
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# 第一步：编译 OpenSSL（提供 HTTPS 支持所需的 libssl.a 和 libcrypto.a）
# 参数 64 表示编译 64 位（arm64-v8a），如需 32 位改为 32
bash "$SCRIPT_DIR/openssl/build_openssl.sh" 64

# 第二步：编译 mp3lame（提供 MP3 编解码支持，产出 libmp3lame.a）
bash "$SCRIPT_DIR/mp3lame/build_lame.sh" 64

# 第三步：编译 x264（提供 H.264 软件编码支持，产出 libx264.a）
bash "$SCRIPT_DIR/x264/build_x264.sh" 64

# 第四步：编译 x265（提供 HEVC/H.265 软件编码支持，产出 libx265.a）
bash "$SCRIPT_DIR/x265/build_x265.sh" 64

# 第五步：编译 FFmpeg，并将上面四个库一起打包进 libffmpeg.so
# FFmpeg configure 会通过 pkg-config 找到上面四个库的头文件和 .a 文件
# 最终产出：$SO_OUT/arm64-v8a/libffmpeg.so
bash "$SCRIPT_DIR/ffmpeg/build_ffmpeg_one.sh"
