#pragma once

#include <memory>
#include <vector>
#include <string>
#include "yig/common/types.h"
#include "yig/common/async.h"

namespace yig::storage {

// ================================
// 存储后端接口 (JuiceFS 存算分离设计)
// ================================

class StorageBackend {
public:
    virtual ~StorageBackend() = default;

    // === 基础操作 ===

    // 写入数据
    virtual AsyncTask<Status> Put(
        const std::string& key,
        const ByteBuffer& data
    ) = 0;

    // 读取数据
    virtual AsyncTask<Status> Get(
        const std::string& key,
        ByteBuffer* data
    ) = 0;

    // 删除数据
    virtual AsyncTask<Status> Delete(
        const std::string& key
    ) = 0;

    // 检查数据是否存在
    virtual AsyncTask<Status> Exists(
        const std::string& key
    ) = 0;

    // === 范围操作 (JuiceFS Slice 风格) ===

    // 读取范围
    virtual AsyncTask<Status> GetRange(
        const std::string& key,
        uint64_t offset,
        uint64_t size,
        ByteBuffer* data
    ) = 0;

    // === AI 场景优化 ===

    // 批量读取
    virtual AsyncTask<Status> BatchGet(
        const std::vector<std::string>& keys,
        std::vector<ByteBuffer>* data
    ) = 0;

    // === 健康检查和容量 ===

    virtual AsyncTask<Status> HealthCheck() = 0;

    struct CapacityInfo {
        uint64_t total_bytes;
        uint64_t used_bytes;
        uint64_t available_bytes;
    };

    virtual AsyncTask<Status> GetCapacity(CapacityInfo* info) = 0;
};

// ================================
// S3 后端实现
// ================================

class S3Backend : public StorageBackend {
public:
    struct Config {
        std::string access_key;
        std::string secret_key;
        std::string region;
        std::string endpoint;    // 可选，用于兼容 S3 的存储
        std::string bucket;
        uint32_t max_connections = 100;
    };

    explicit S3Backend(Config config);
    ~S3Backend() override = default;

    // === 实现 StorageBackend 接口 ===

    AsyncTask<Status> Put(
        const std::string& key,
        const ByteBuffer& data
    ) override;

    AsyncTask<Status> Get(
        const std::string& key,
        ByteBuffer* data
    ) override;

    AsyncTask<Status> Delete(
        const std::string& key
    ) override;

    AsyncTask<Status> Exists(
        const std::string& key
    ) override;

    AsyncTask<Status> GetRange(
        const std::string& key,
        uint64_t offset,
        uint64_t size,
        ByteBuffer* data
    ) override;

    AsyncTask<Status> BatchGet(
        const std::vector<std::string>& keys,
        std::vector<ByteBuffer>* data
    ) override;

    AsyncTask<Status> HealthCheck() override;
    AsyncTask<Status> GetCapacity(CapacityInfo* info) override;

private:
    Config config_;

    // AWS S3 客户端 (需要 libaws-cpp-sdk)
    class S3Client;
    std::unique_ptr<S3Client> client_;
};

// ================================
// 本地文件系统后端 (开发/测试)
// ================================

class LocalBackend : public StorageBackend {
public:
    struct Config {
        std::string data_dir;  // 数据根目录
    };

    explicit LocalBackend(Config config);
    ~LocalBackend() override = default;

    // === 实现 StorageBackend 接口 ===

    AsyncTask<Status> Put(
        const std::string& key,
        const ByteBuffer& data
    ) override;

    AsyncTask<Status> Get(
        const std::string& key,
        ByteBuffer* data
    ) override;

    AsyncTask<Status> Delete(
        const std::string& key
    ) override;

    AsyncTask<Status> Exists(
        const std::string& key
    ) override;

    AsyncTask<Status> GetRange(
        const std::string& key,
        uint64_t offset,
        uint64_t size,
        ByteBuffer* data
    ) override;

    AsyncTask<Status> BatchGet(
        const std::vector<std::string>& keys,
        std::vector<ByteBuffer>* data
    ) override;

    AsyncTask<Status> HealthCheck() override;
    AsyncTask<Status> GetCapacity(CapacityInfo* info) override;

private:
    Config config_;

    // key 转换为文件路径
    std::string KeyToPath(const std::string& key);
};

} // namespace yig::storage
