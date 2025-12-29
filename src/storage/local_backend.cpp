// ================================
// 空实现骨架 - 等你来完善
// ================================

#include "nebulastore/storage/backend.h"

namespace nebulastore::storage {

// ================================
// LocalBackend
// ================================

LocalBackend::LocalBackend(const Config& config)
    : config_(config) {}

LocalBackend::~LocalBackend() = default;

AsyncTask<Status> LocalBackend::Put(
    const std::string& key,
    const ByteBuffer& data
) {
    // TODO: 实现写入逻辑
    co_return Status::OK();
}

AsyncTask<Status> LocalBackend::Get(
    const std::string& key,
    ByteBuffer* data
) {
    // TODO: 实现读取逻辑
    co_return Status::OK();
}

std::string LocalBackend::KeyToPath(const std::string& key) {
    // key 格式: chunks/{inode}/{slice}
    // 转换为: {data_dir}/chunks/{inode}/{slice}
    std::string path = config_.data_dir;
    path.push_back('/');
    path.append(key);
    return path;
}

} // namespace nebulastore::storage
