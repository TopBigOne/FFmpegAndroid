#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/../my_config.sh"

make clean 2>/dev/null

archbit=${1:-64}

if [ $archbit -eq 32 ]; then
  echo "build for 32bit"
  API=21
  ABI='armeabi-v7a'
  CPU='arm'
  ANDROID='androideabi'
else
  echo "build for 64bit"
  API=21
  ABI='arm64-v8a'
  CPU='aarch64'
  ANDROID='android'
fi

uname=$(uname)
if [ "$uname" = "Darwin" ]; then
  COMPILE_OS="darwin"
elif [ "$uname" = "Linux" ]; then
  COMPILE_OS="linux"
else
  echo "don't support $uname"; exit 1
fi

export TOOLCHAIN=$NDK_ROOT/toolchains/llvm/prebuilt/$COMPILE_OS-x86_64
export CC=$TOOLCHAIN/bin/$CPU-linux-$ANDROID$API-clang
export CXX=$TOOLCHAIN/bin/$CPU-linux-$ANDROID$API-clang++
export AR=$TOOLCHAIN/bin/llvm-ar
export LD=$TOOLCHAIN/bin/ld

export PREFIX=$SO_OUT/$ABI

cd "$X264_SRC"

function build_x264() {
  ./configure \
  --prefix=$PREFIX \
  --enable-static \
  --disable-asm \
  --enable-pic \
  --host=$CPU-linux-$ANDROID \
  --cross-prefix=$TOOLCHAIN/bin/llvm- \
  --sysroot=$TOOLCHAIN/sysroot \
  --extra-cflags="-Os -fPIC" \
  --extra-ldflags="-Wl,-z,max-page-size=16384"

  make -j$(getconf _NPROCESSORS_ONLN)
  make install
}

build_x264
echo "build x264 done, output: $PREFIX"
