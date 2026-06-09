#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/../my_config.sh"

archbit=${1:-64}

if [ $archbit -eq 32 ]; then
  echo "build for 32bit"
  CPU='arm'
  ABI='armeabi-v7a'
  API=21
else
  echo "build for 64bit"
  CPU='arm64'
  ABI='arm64-v8a'
  API=21
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
export ANDROID_NDK_ROOT=$NDK_ROOT
export PATH=$TOOLCHAIN/bin:$PATH

export PREFIX=$SO_OUT/$ABI

export LDFLAGS="-Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384"

cd "$openssl_SRC"
make clean 2>/dev/null

function build() {
  # -fPIC 直接写入 Configure 命令行，确保对 C 文件和汇编 .S 文件都生效。
  # 用 export CFLAGS 方式时，OpenSSL Makefile 内部对汇编文件的编译路径会覆盖
  # 环境变量，导致 poly1305-armv9-sve2.S 等文件未加 -fPIC 而引发链接错误。
  ./Configure android-$CPU \
  -D__ANDROID_API__=$API \
  no-shared \
  no-comp \
  -fPIC \
  --prefix=$PREFIX \
  --openssldir=$PREFIX

  make -j$(getconf _NPROCESSORS_ONLN)
  make install_sw
}

build
echo "✅✅✅build openssl done, output: $PREFIX"
