#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/../my_config.sh"

archbit=${1:-64}

if [ $archbit -eq 32 ]; then
  echo "build for 32bit"
  ARCH='arm'
  CPU='armv7-a'
  ABI='armeabi-v7a'
  API=21
  ANDROID='androideabi'
else
  echo "build for 64bit"
  ARCH='aarch64'
  CPU='armv8-a'
  ABI='arm64-v8a'
  API=21
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

export TOOLCHAIN=$NDK_ROOT/toolchains/llvm/prebuilt/$COMPILE_OS-x86_64/bin
export CC=$TOOLCHAIN/$ARCH-linux-$ANDROID$API-clang
export CXX=$TOOLCHAIN/$ARCH-linux-$ANDROID$API-clang++
export AR=$TOOLCHAIN/llvm-ar
export RANLIB=$TOOLCHAIN/llvm-ranlib
export STRIP=$TOOLCHAIN/llvm-strip
export LD=$TOOLCHAIN/ld

# Output to same prefix FFmpeg expects
export PREFIX=$SO_OUT/$ABI

cd "$MP3LAME_SRC"

AUTOMAKE_LIBDIR=$(automake --print-libdir 2>/dev/null)
if [ -n "$AUTOMAKE_LIBDIR" ]; then
  cp "$AUTOMAKE_LIBDIR/config.sub" ./config.sub
  cp "$AUTOMAKE_LIBDIR/config.guess" ./config.guess
fi

function build_lame() {
  ./configure \
  --build=aarch64-apple-darwin \
  --host=$ARCH-linux-$ANDROID \
  --prefix=$PREFIX \
  --enable-static \
  --disable-shared \
  --disable-frontend \
  LDFLAGS="-Wl,-z,max-page-size=16384"
  make -j$(getconf _NPROCESSORS_ONLN)
  make install
}

build_lame
echo "building mp3lame done, output: $PREFIX"
