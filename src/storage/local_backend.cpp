// ================================
// 本地文件系统后端实现
// 用于开发/测试
// ================================

#include "nebulastore/storage/backend.h"
#include "nebulastore/common/logger.h"
#include <fstream>
#include <filesystem>
#include <sstream>

namespace nebulastore::storage {

// ================================
// LocalBackend
// ================================

LocalBackend::LocalBackend(Config config)
    : config_(std::move(config)) {
    // 确保数据目录存在
    std::filesystem::create_directories(config_.data_dir);
    LOG_INFO("LocalBackend initialized: %s", config_.data_dir.c_str());
}

std::string LocalBackend::KeyToPath(const std::string& key) {
    // key 格式: chunks/{inode}/{slice}
    // 转换为: {data_dir}/chunks/{inode}/{slice}
    std::string path = config_.data_dir;
    if (!path.empty() && path.back() != '/') {
        path.push_back('/');
    }
    path.append(key);
    return path;
}

AsyncTask<Status> LocalBackend::Put(
    const std::string& key,
    const ByteBuffer& data
) {
    auto path = KeyToPath(key);

    // 创建父目录
    auto parent_path = std::filesystem::path(path).parent_path();
    std::error_code ec;
    std::filesystem::create_directories(parent_path, ec);
    if (ec && ec != std::errc::file_exists) {
        LOG_ERROR("Failed to create directory: %s", ec.message().c_str());
        co_return Status::IO("Failed to create directory: " + ec.message());
    }

    // 写入文件
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        LOG_ERROR("Failed to open file for writing: %s", path.c_str());
        co_return Status::IO("Failed to open file: " + path);
    }

    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    if (!file) {
        LOG_ERROR("Failed to write file: %s", path.c_str());
        co_return Status::IO("Failed to write file: " + path);
    }

    LOG_DEBUG("Written %zu bytes to %s", data.size(), path.c_str());
    co_return Status::Ok();
}

AsyncTask<Status> LocalBackend::Get(
    const std::string& key,
    ByteBuffer* data
) {
    auto path = KeyToPath(key);

    // 读取文件
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        LOG_ERROR("Failed to open file for reading: %s", path.c_str());
        co_return Status::NotFound("File not found: " + key);
    }

    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    file.read(reinterpret_cast<char*>(buffer.data()), size);

    if (!file) {
        LOG_ERROR("Failed to read file: %s", path.c_str());
        co_return Status::IO("Failed to read file: " + path);
    }

    if (data) {
        data->assign(std::move(buffer));
    }

    LOG_DEBUG("Read %ld bytes from %s", static_cast<long>(size), path.c_str());
    co_return Status::Ok();
}

AsyncTask<Status> LocalBackend::Delete(
    const std::string& key
) {
    auto path = KeyToPath(key);

    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) {
        LOG_ERROR("Failed to delete file: %s", ec.message().c_str());
        co_return Status::IO("Failed to delete file: " + ec.message());
    }

    LOG_DEBUG("Deleted: %s", path.c_str());
    co_return Status::Ok();
}

AsyncTask<Status> LocalBackend::Exists(
    const std::string& key
) {
    auto path = KeyToPath(key);

    std::error_code ec;
    bool exists = std::filesystem::exists(path, ec);

    if (ec) {
        co_return Status::IO("Failed to check file existence");
    }

    co_return exists ? Status::Ok() : Status::NotFound("File not found");
}

AsyncTask<Status> LocalBackend::GetRange(
    const std::string& key,
    uint64_t offset,
    uint64_t size,
    ByteBuffer* data
) {
    auto path = KeyToPath(key);

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        co_return Status::NotFound("File not found: " + key);
    }

    // 跳到偏移位置
    file.seekg(offset, std::ios::beg);
    if (!file) {
        co_return Status::InvalidArgument("Invalid offset");
    }

    // 读取指定大小
    std::vector<uint8_t> buffer(size);
    file.read(reinterpret_cast<char*>(buffer.data()), size);

    auto bytes_read = file.gcount();
    if (!file && !file.eof()) {
        co_return Status::IO("Failed to read file range");
    }

    if (data) {
        buffer.resize(bytes_read);
        data->assign(std::move(buffer));
    }

    LOG_DEBUG("Read range %lu+%lu from %s", offset, size, path.c_str());
    co_return Status::Ok();
}

AsyncTask<Status> LocalBackend::BatchGet(
    const std::vector<std::string>& keys,
    std::vector<ByteBuffer>* data
) {
    data->resize(keys.size());

    for (size_t i = 0; i < keys.size(); ++i) {
        auto status = co_await Get(keys[i], &(*data)[i]);
        if (!status.OK()) {
            co_return status;
        }
    }

    co_return Status::Ok();
}

AsyncTask<Status> LocalBackend::HealthCheck() {
    // 检查数据目录是否可访问
    std::error_code ec;
    bool exists = std::filesystem::exists(config_.data_dir, ec);

    if (ec || !exists) {
        co_return Status::IO("Data directory not accessible");
    }

    co_return Status::Ok();
}

AsyncTask<Status> LocalBackend::GetCapacity(CapacityInfo* info) {
    std::error_code ec;

    auto space = std::filesystem::space(config_.data_dir, ec);
    if (ec) {
        co_return Status::IO("Failed to get disk space");
    }

    info->total_bytes = space.capacity;
    info->available_bytes = space.available;
    info->used_bytes = space.capacity - space.available;

    co_return Status::Ok();
}

} // namespace nebulastore::storage
