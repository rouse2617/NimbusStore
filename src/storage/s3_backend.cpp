// ================================
// 空实现骨架 - 等你来完善
// ================================

#include "nebulastore/storage/backend.h"

namespace nebulastore::storage {

// ================================
// S3Backend
// ================================

S3Backend::S3Backend(Config config)
    : config_(std::move(config)) {}

S3Backend::~S3Backend() = default;

AsyncTask<Status> S3Backend::Put(
    const std::string& key,
    const ByteBuffer& data
) {
    // TODO: 使用 AWS C++ SDK 实现
    co_return Status::OK();
}

AsyncTask<Status> S3Backend::Get(
    const std::string& key,
    ByteBuffer* data
) {
    // TODO: 使用 AWS C++ SDK 实现
    co_return Status::OK();
}

AsyncTask<Status> S3Backend::Delete(
    const std::string& key
) {
    // TODO: 使用 AWS C++ SDK 实现
    co_return Status::OK();
}

AsyncTask<Status> S3Backend::Exists(
    const std::string& key
) {
    // TODO: 使用 AWS C++ SDK 实现
    co_return Status::OK();
}

AsyncTask<Status> S3Backend::GetRange(
    const std::string& key,
    uint64_t offset,
    uint64_t size,
    ByteBuffer* data
) {
    // TODO: 使用 AWS C++ SDK 实现范围读取
    co_return Status::OK();
}

AsyncTask<Status> S3Backend::BatchGet(
    const std::vector<std::string>& keys,
    std::vector<ByteBuffer>* data
) {
    // TODO: 并发批量读取
    co_return Status::OK();
}

AsyncTask<Status> S3Backend::HealthCheck() {
    co_return Status::OK();
}

AsyncTask<Status> S3Backend::GetCapacity(CapacityInfo* info) {
    // S3 存储容量无限
    info->total_bytes = 0;
    info->used_bytes = 0;
    info->available_bytes = 0;
    co_return Status::OK();
}

} // namespace nebulastore::storage
