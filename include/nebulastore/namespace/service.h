#pragma once

#include <memory>
#include <vector>
#include <string>
#include "nebulastore/common/types.h"
#include "nebulastore/common/async.h"
#include "nebulastore/metadata/metadata_service.h"
#include "nebulastore/storage/backend.h"

namespace nebulastore::namespace_ {

// ================================
// 路径转换器 (统一命名空间核心)
// ================================

class PathConverter {
public:
    struct ParsedPath {
        bool is_s3;           // 是否为 S3 路径
        std::string bucket;   // S3 bucket
        std::string key;      // S3 key 或 POSIX 路径
        std::string posix_path;  // POSIX 格式路径
    };

    explicit PathConverter(const std::string& default_bucket = "default")
        : default_bucket_(default_bucket) {}

    // S3 → POSIX: s3://bucket/data/file.txt → /data/file.txt
    std::string S3ToPosix(const std::string& s3_path);

    // POSIX → S3: /data/file.txt → s3://bucket/data/file.txt
    std::string PosixToS3(const std::string& posix_path);

    // 统一解析
    ParsedPath Parse(const std::string& path);

private:
    std::string default_bucket_;
};

// ================================
// 统一命名空间服务
// ================================

class NamespaceService {
public:
    struct Config {
        std::shared_ptr<metadata::MetadataService> metadata_service;
        std::shared_ptr<storage::StorageBackend> storage_backend;
        std::string default_bucket = "default";
    };

    explicit NamespaceService(Config config);
    ~NamespaceService() = default;

    // === 统一查询接口 ===

    // 通过任意路径格式获取文件属性
    AsyncTask<Status> GetAttr(
        const std::string& path,
        InodeAttr* attr
    );

    // 获取文件布局
    AsyncTask<Status> GetLayout(
        const std::string& path,
        FileLayout* layout
    );

    // 读取文件
    AsyncTask<Status> Read(
        const std::string& path,
        uint64_t offset,
        uint64_t size,
        ByteBuffer* data
    );

    // 写入文件
    AsyncTask<Status> Write(
        const std::string& path,
        const ByteBuffer& data,
        uint64_t offset
    );

    // 列出目录
    AsyncTask<Status> Readdir(
        const std::string& path,
        std::vector<Dentry>* entries
    );

private:
    PathConverter converter_;
    std::shared_ptr<metadata::MetadataService> metadata_service_;
    std::shared_ptr<storage::StorageBackend> storage_backend_;
};

} // namespace nebulastore::namespace_
