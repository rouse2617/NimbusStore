// ================================
// 空实现骨架 - 等你来完善
// ================================

#include "yig/namespace/service.h"

namespace yig::namespace_ {

// ================================
// PathConverter
// ================================

std::string PathConverter::S3ToPosix(const std::string& s3_path) {
    // s3://bucket/data/file.txt → /data/file.txt
    if (s3_path.substr(0, 5) != "s3://") {
        return s3_path;
    }
    auto pos = s3_path.find('/', 5);
    if (pos == std::string::npos) {
        return "/";
    }
    return s3_path.substr(pos);
}

std::string PathConverter::PosixToS3(const std::string& posix_path) {
    // /data/file.txt → s3://bucket/data/file.txt
    return "s3://" + default_bucket_ + posix_path;
}

PathConverter::ParsedPath PathConverter::Parse(const std::string& path) {
    ParsedPath result;
    if (path.substr(0, 5) == "s3://") {
        result.is_s3 = true;
        auto pos = path.find('/', 5);
        result.bucket = path.substr(5, pos - 5);
        result.key = path.substr(pos + 1);
        result.posix_path = "/" + result.key;
    } else {
        result.is_s3 = false;
        result.posix_path = path;
        result.bucket = default_bucket_;
        result.key = path.substr(1);
    }
    return result;
}

// ================================
// NamespaceService
// ================================

NamespaceService::NamespaceService(Config config)
    : converter_(config.default_bucket),
      metadata_service_(config.metadata_service),
      storage_backend_(config.storage_backend) {}

} // namespace yig::namespace_
