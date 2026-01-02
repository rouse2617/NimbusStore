#include "nebulastore/namespace/service.h"

namespace nebulastore::namespace_ {

// ================================
// PathConverter
// ================================

std::string PathConverter::S3ToPosix(const std::string& s3_path) {
    if (s3_path.compare(0, 5, "s3://") != 0) {
        return s3_path;
    }
    auto pos = s3_path.find('/', 5);
    if (pos == std::string::npos) {
        return "/";
    }
    return s3_path.substr(pos);
}

std::string PathConverter::PosixToS3(const std::string& posix_path) {
    return "s3://" + default_bucket_ + posix_path;
}

PathConverter::ParsedPath PathConverter::Parse(const std::string& path) {
    ParsedPath result;
    if (path.compare(0, 5, "s3://") == 0) {
        result.is_s3 = true;
        auto pos = path.find('/', 5);
        if (pos == std::string::npos) {
            result.bucket = path.substr(5);
            result.key = "";
            result.posix_path = "/";
        } else {
            result.bucket = path.substr(5, pos - 5);
            result.key = path.substr(pos + 1);
            result.posix_path = "/" + result.key;
        }
    } else {
        result.is_s3 = false;
        result.posix_path = path;
        result.bucket = default_bucket_;
        result.key = (path.size() > 1) ? path.substr(1) : "";
    }
    return result;
}

// ================================
// NamespaceService
// ================================

NamespaceService::NamespaceService(Config config)
    : converter_(config.default_bucket),
      metadata_service_(std::move(config.metadata_service)),
      storage_backend_(std::move(config.storage_backend)) {}

AsyncTask<Status> NamespaceService::GetAttr(const std::string& path, InodeAttr* attr) {
    auto parsed = converter_.Parse(path);
    co_return co_await metadata_service_->GetAttr(parsed.posix_path, attr);
}

AsyncTask<Status> NamespaceService::GetLayout(const std::string& path, FileLayout* layout) {
    auto parsed = converter_.Parse(path);
    InodeID inode_id;
    auto status = co_await metadata_service_->LookupPath(parsed.posix_path, &inode_id);
    if (!status.OK()) {
        co_return status;
    }
    co_return co_await metadata_service_->GetLayout(inode_id, layout);
}

AsyncTask<Status> NamespaceService::Read(const std::string& path, uint64_t offset,
                                          uint64_t size, ByteBuffer* data) {
    auto parsed = converter_.Parse(path);
    InodeID inode_id;
    auto status = co_await metadata_service_->LookupPath(parsed.posix_path, &inode_id);
    if (!status.OK()) {
        co_return status;
    }

    FileLayout layout;
    status = co_await metadata_service_->GetLayout(inode_id, &layout);
    if (!status.OK()) {
        co_return status;
    }

    // Find relevant slices
    for (const auto& slice : layout.slices) {
        if (offset >= slice.offset && offset < slice.offset + slice.size) {
            uint64_t slice_offset = offset - slice.offset;
            uint64_t read_size = std::min(size, slice.size - slice_offset);
            co_return co_await storage_backend_->GetRange(slice.storage_key, slice_offset, read_size, data);
        }
    }
    co_return Status::NotFound("No slice found for offset");
}

AsyncTask<Status> NamespaceService::Write(const std::string& path, const ByteBuffer& data,
                                           uint64_t offset) {
    auto parsed = converter_.Parse(path);
    InodeID inode_id;
    auto status = co_await metadata_service_->LookupPath(parsed.posix_path, &inode_id);
    if (!status.OK()) {
        co_return status;
    }

    // Generate storage key
    std::string storage_key = "chunks/" + std::to_string(inode_id) + "/" + std::to_string(offset);
    status = co_await storage_backend_->Put(storage_key, data);
    if (!status.OK()) {
        co_return status;
    }

    // Add slice metadata
    SliceInfo slice{0, offset, data.size(), storage_key};
    status = co_await metadata_service_->AddSlice(inode_id, slice);
    if (!status.OK()) {
        co_return status;
    }

    co_return co_await metadata_service_->UpdateSize(inode_id, offset + data.size());
}

AsyncTask<Status> NamespaceService::Readdir(const std::string& path,
                                             std::vector<Dentry>* entries) {
    auto parsed = converter_.Parse(path);
    co_return co_await metadata_service_->Readdir(parsed.posix_path, entries);
}

} // namespace nebulastore::namespace_
