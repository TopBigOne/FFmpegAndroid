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

# lame 的 configure.ac 会在内部重写 CFLAGS，用 configure 参数传入的 CFLAGS 会被覆盖。
# 正确做法是先 export 到环境变量，configure 读取环境变量作为初始值再扩展，-fPIC 才能保留。
export CFLAGS="-Os -fPIC"
export LDFLAGS="-Wl,-z,max-page-size=16384"

function build_lame() {
  # 清除旧的 .o 文件，防止非 -fPIC 的旧目标文件被 make 判断为最新而跳过重编
  make clean 2>/dev/null
  ./configure \
  --build=aarch64-apple-darwin \
  --host=$ARCH-linux-$ANDROID \
  --prefix=$PREFIX \
  --enable-static \
  --disable-shared \
  --disable-frontend
  make -j$(getconf _NPROCESSORS_ONLN)
  make install
}

build_lame
echo "✅✅✅build mp3lame done, output: $PREFIX"
