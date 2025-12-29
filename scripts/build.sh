#!/bin/bash

# YIG 2.0 构建脚本

set -e

echo "======================================="
echo "YIG 2.0 Build Script"
echo "======================================="

# 检查依赖
echo "Checking dependencies..."

command -v cmake >/dev/null 2>&1 || { echo "Error: cmake not found"; exit 1; }
command -v g++ >/dev/null 2>&1 || { echo "Error: g++ not found"; exit 1; }

# 检查 RocksDB
if [ ! -f "/usr/include/rocksdb/db.h" ]; then
    echo "Error: RocksDB not found"
    echo "Install: sudo apt install librocksdb-dev"
    exit 1
fi

echo "All dependencies OK"

# 构建目录
BUILD_DIR=${BUILD_DIR:-build}
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 配置
echo "Configuring..."
cmake \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DYIG_BUILD_TESTS=ON \
    ..

# 编译
echo "Building..."
make -j$(nproc)

echo "======================================="
echo "Build complete!"
echo "Binaries: $BUILD_DIR/"
echo "======================================="
