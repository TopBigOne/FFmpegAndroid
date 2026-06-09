#!/bin/bash

# 获取本脚本所在的绝对目录
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# 加载公共配置：NDK_ROOT、X265_SRC、SO_OUT 等
source "$SCRIPT_DIR/../my_config.sh"

# 目标架构位数，默认 64 位
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

# 产物输出目录（与 openssl/lame/x264/ffmpeg 保持一致）
export PREFIX=$SO_OUT/$ABI

# x265 使用 CMake，必须在源码目录之外单独建一个 build 目录（out-of-source build）
# 每次重新编译前先清空，避免旧的 CMakeCache 干扰
BUILD_DIR=$X265_SRC/build_android_$ABI
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

function build_x265() {
  cd "$BUILD_DIR"

  # x265 源码入口是 source/ 子目录，不是根目录
  # 不使用 NDK toolchain 文件，直接指定 target-prefixed 的 clang wrapper。
  # 原因：x265 用 add_custom_command 编译 .S 汇编文件时直接调用
  #       ${CMAKE_CXX_COMPILER}，cmake 不会自动追加 --target 等交叉编译标志。
  #       使用 aarch64-linux-android21-clang++ 这个 wrapper，它内部已硬编码
  #       --target=aarch64-linux-android21 和 --sysroot，从根本上解决汇编器
  #       以 macOS 模式处理 Android AArch64 .S 文件的问题。
  #
  # 各参数说明：
  #   CMAKE_SYSTEM_NAME=Android          目标系统
  #   CMAKE_SYSTEM_PROCESSOR=aarch64     目标 CPU
  #   CMAKE_C/CXX_COMPILER               NDK target-prefixed clang wrapper
  #   CMAKE_AR/RANLIB/STRIP              NDK llvm 工具链
  #   CMAKE_FIND_ROOT_PATH               sysroot 路径，库/头文件从这里找
  #   ENABLE_SHARED=OFF                  只编译静态库 libx265.a
  #   ENABLE_CLI=OFF                     不编译命令行工具
  #   ENABLE_ASSEMBLY=OFF                关闭 x86 nasm 汇编
  #   CROSS_COMPILE_ARM=1                告知 x265 这是 ARM 交叉编译
  #   EXPORT_C_API=ON                    导出 C API，FFmpeg 调用需要
  #   HIGH_BIT_DEPTH=OFF                 8-bit 编码（main profile）
  #   CMAKE_*_LINKER_FLAGS               页大小对齐 16KB（Android 15+ 要求）
  cmake "$X265_SRC/source" \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER=$TOOLCHAIN/bin/${CPU}-linux-${ANDROID}${API}-clang \
    -DCMAKE_CXX_COMPILER=$TOOLCHAIN/bin/${CPU}-linux-${ANDROID}${API}-clang++ \
    -DCMAKE_AR=$TOOLCHAIN/bin/llvm-ar \
    -DCMAKE_RANLIB=$TOOLCHAIN/bin/llvm-ranlib \
    -DCMAKE_STRIP=$TOOLCHAIN/bin/llvm-strip \
    -DCMAKE_FIND_ROOT_PATH=$TOOLCHAIN/sysroot \
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=$PREFIX \
    -DENABLE_SHARED=OFF \
    -DENABLE_CLI=OFF \
    -DENABLE_ASSEMBLY=OFF \
    -DCROSS_COMPILE_ARM=1 \
    -DEXPORT_C_API=ON \
    -DHIGH_BIT_DEPTH=OFF \
    -DCMAKE_C_FLAGS="-Os -fPIC" \
    -DCMAKE_CXX_FLAGS="-Os -fPIC" \
    -DCMAKE_EXE_LINKER_FLAGS="-Wl,-z,max-page-size=16384" \
    -DCMAKE_SHARED_LINKER_FLAGS="-Wl,-z,max-page-size=16384"

  make -j$(getconf _NPROCESSORS_ONLN)
  make install

  # cmake 交叉编译 bug：生成的 x265.pc 里 Libs.private 包含 -l-l:libunwind.a
  # （多了一个 -l 前缀），导致 pkg-config --exists x265 返回失败。
  # 修复：把 Libs.private 改成干净的依赖列表，libunwind 由 -lc++_static 间接覆盖。
  sed -i '' 's/^Libs.private:.*/Libs.private: -lc++ -lm -ldl/' \
    "$PREFIX/lib/pkgconfig/x265.pc"
}

build_x265
echo "✅✅✅build x265 done, output: $PREFIX"
