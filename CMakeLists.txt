cmake_minimum_required(VERSION 3.20)
project(NebulaStore VERSION 2.0.0 LANGUAGES CXX)

# ================================
# C++ 标准和编译选项
# ================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wall -Wextra -Werror)
endif()

# ================================
# 依赖库
# ================================
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# RocksDB (需要手动安装)
# apt install librocksdb-dev libbz2-dev liblz4-dev libsnappy-dev

# gRPC (可选，用于 RPC)
# find_package(gRPC CONFIG REQUIRED)

# ================================
# 全局头文件路径
# ================================
include_directories(${PROJECT_SOURCE_DIR}/include)

# ================================
# 子目录
# ================================
add_subdirectory(src)

# ================================
# 测试
# ================================
option(NEBULA_BUILD_TESTS "Build tests" ON)
if(NEBULA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
