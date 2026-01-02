#pragma once

#include <cstdint>
#include <array>
#include <cstddef>

namespace nebulastore {

// ================================
// 子系统定义 (参考 Ceph)
// ================================

enum class SubsysID : uint32_t {
    _default = 0,      // 默认
    metadata,          // 元数据服务
    rocksdb,           // RocksDB 存储
    storage,           // 存储后端
    local_backend,     // 本地后端
    s3_backend,        // S3 后端
    http_server,       // HTTP 服务器
    s3_handler,        // S3 协议处理
    raft,              // Raft 共识
    partition,         // 分区管理
    cache,             // 缓存系统
    async_runtime,     // 异步运行时
    max
};

// 子系统配置
struct SubsysItem {
    const char* name;
    uint8_t log_level;    // 日志级别
    uint8_t gather_level; // 收集级别
};

// 子系统默认配置
constexpr SubsysItem kSubsysConfig[] = {
    { "_default", 0, 5 },
    { "metadata", 1, 5 },
    { "rocksdb", 4, 5 },
    { "storage", 1, 5 },
    { "local_backend", 1, 5 },
    { "s3_backend", 1, 5 },
    { "http_server", 1, 5 },
    { "s3_handler", 1, 5 },
    { "raft", 1, 5 },
    { "partition", 1, 5 },
    { "cache", 1, 5 },
    { "async_runtime", 1, 5 },
};

constexpr size_t kSubsysCount = static_cast<size_t>(SubsysID::max);

} // namespace nebulastore
