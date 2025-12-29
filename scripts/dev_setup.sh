#!/bin/bash
# NebulaStore 2.0 Development Environment Setup
# 适用于 Ubuntu 22.04 / Debian 12

set -e

echo "======================================"
echo "NebulaStore 2.0 开发环境安装"
echo "======================================"

# 检测系统
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
    VERSION=$VERSION_ID
else
    echo "无法检测操作系统"
    exit 1
fi

echo "检测到系统: $OS $VERSION"

# ================================
# 1. 基础依赖
# ================================
echo ""
echo "[1/6] 安装基础编译工具..."

if [ "$OS" = "ubuntu" ] || [ "$OS" = "debian" ]; then
    sudo apt update
    sudo apt install -y \
        build-essential \
        cmake \
        git \
        wget \
        curl \
        pkg-config \
        libtool \
        autoconf \
        automake
fi

# ================================
# 2. RocksDB 安装
# ================================
echo ""
echo "[2/6] 安装 RocksDB..."

ROCKSDB_VERSION=8.10.0
ROCKSDB_INSTALL_DIR=/usr/local

if [ ! -f "${ROCKSDB_INSTALL_DIR}/lib/librocksdb.so" ]; then
    cd /tmp
    wget -q https://github.com/facebook/rocksdb/archive/refs/tags/v${ROCKSDB_VERSION}.tar.gz
    tar -xzf v${ROCKSDB_VERSION}.tar.gz
    cd rocksdb-${ROCKSDB_VERSION}

    # 编译共享库
    make shared -j$(nproc)
    make install-shared

    # 配置库路径
    echo "${ROCKSDB_INSTALL_DIR}/lib" | sudo tee /etc/ld.so.conf.d/rocksdb.conf
    sudo ldconfig

    cd /
    rm -rf /tmp/rocksdb-*
    echo "RocksDB ${ROCKSDB_VERSION} 安装完成"
else
    echo "RocksDB 已安装，跳过"
fi

# ================================
# 3. spdlog 日志库
# ================================
echo ""
echo "[3/6] 安装 spdlog..."

if [ ! -f "${ROCKSDB_INSTALL_DIR}/lib/libspdlog.so" ]; then
    cd /tmp
    SPDLOG_VERSION=1.13.0
    wget -q https://github.com/gabime/spdlog/archive/refs/tags/v${SPDLOG_VERSION}.tar.gz
    tar -xzf v${SPDLOG_VERSION}.tar.gz
    cd spdlog-${SPDLOG_VERSION}

    mkdir build && cd build
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=${ROCKSDB_INSTALL_DIR} \
        -DBUILD_SHARED_LIBS=ON

    make -j$(nproc)
    sudo make install

    cd /
    rm -rf /tmp/spdlog-*
    echo "spdlog ${SPDLOG_VERSION} 安装完成"
else
    echo "spdlog 已安装，跳过"
fi

# ================================
# 4. FUSE (用于 POSIX 文件系统)
# ================================
echo ""
echo "[4/6] 安装 FUSE3..."

if [ "$OS" = "ubuntu" ] || [ "$OS" = "debian" ]; then
    sudo apt install -y libfuse3-dev fuse3
fi

# ================================
# 5. AWS SDK (用于 S3 后端，可选)
# ================================
echo ""
echo "[5/6] 安装 AWS SDK for C++ (可选，按 Ctrl+C 跳过)..."

# 检查是否已跳过
set +e
read -t 5 -n 1 -p "是否安装 AWS SDK? [Y/n]: " install_aws
set -e
echo ""

if [[ "$install_aws" != "n" && "$install_aws" != "N" ]]; then
    if [ ! -f "${ROCKSDB_INSTALL_DIR}/lib/libaws-cpp-sdk-core.so" ]; then
        cd /tmp
        git clone --depth 1 --branch 1.11.0 https://github.com/aws/aws-sdk-cpp.git
        cd aws-sdk-cpp

        mkdir build && cd build
        cmake .. \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX=${ROCKSDB_INSTALL_DIR} \
            -DBUILD_ONLY=s3 \
            -DENABLE_TESTING=OFF

        make -j$(nproc)
        sudo make install

        cd /
        rm -rf /tmp/aws-sdk-cpp-*
        echo "AWS SDK 安装完成"
    else
        echo "AWS SDK 已安装，跳过"
    fi
else
    echo "跳过 AWS SDK 安装"
fi

# ================================
# 6. 验证安装
# ================================
echo ""
echo "[6/6] 验证安装..."

echo "CMake 版本:"
cmake --version

echo ""
echo "RocksDB:"
ldconfig -p | grep rocksdb || echo "  未找到"

echo ""
echo "spdlog:"
ldconfig -p | grep spdlog || echo "  未找到"

echo ""
echo "FUSE3:"
pkg-config --modversion fuse3 2>/dev/null || echo "  未找到"

# ================================
# 7. 编译 NebulaStore
# ================================
echo ""
echo "======================================"
echo "准备编译 NebulaStore"
echo "======================================"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_DIR"

mkdir -p build
cd build

echo ""
echo "运行 CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DNEBULA_BUILD_TESTS=ON

echo ""
echo "编译..."
make -j$(nproc)

echo ""
echo "======================================"
echo "安装完成！"
echo "======================================"
echo ""
echo "运行测试:"
echo "  cd build && ./tests/stage0_test"
echo ""
echo "运行 FUSE:"
echo "  ./nebula-fuse /mnt/nebula /tmp/nebula.db"
echo ""
echo "运行 S3 Gateway:"
echo "  ./nebula-s3-gateway --config ../configs/s3_gateway.yaml"
