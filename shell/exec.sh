#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

bash "$SCRIPT_DIR/openssl/build_openssl.sh" 64
bash "$SCRIPT_DIR/mp3lame/build_lame.sh" 64
bash "$SCRIPT_DIR/x264/build_x264.sh" 64
bash "$SCRIPT_DIR/ffmpeg/build_ffmpeg_one.sh"
