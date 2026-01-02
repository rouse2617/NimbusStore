// ================================
// S3 Gateway Implementation
// ================================

#include "nebulastore/protocol/gateway.h"
#include "nebulastore/protocol/http_server.h"
#include "nebulastore/common/logger_v2.h"
#include "nebulastore/common/dout.h"
#include <sstream>
#include <iomanip>
#include <regex>
#include <ctime>

namespace nebulastore::protocol {

// ================================
// S3Gateway::HTTPServer Implementation
// ================================

class S3Gateway::HTTPServer {
public:
    HTTPServer(S3Gateway* gateway, const Config& config)
        : gateway_(gateway)
        , http_server_(config.host, config.port) {}

    bool Start() {
        RegisterRoutes();
        return http_server_.Start();
    }

    void Stop() { http_server_.Stop(); }

private:
    S3Gateway* gateway_;
    HttpServer http_server_;

    void RegisterRoutes() {
        // Service operations
        http_server_.RegisterHandler("GET", "/", [this](auto& m, auto& p, auto& b) {
            return HandleListBuckets(m, p, b);
        });

        // Bucket/Object operations use wildcard matching in handler
        http_server_.RegisterHandler("GET", "/*", [this](auto& m, auto& p, auto& b) {
            return HandleGet(m, p, b);
        });
        http_server_.RegisterHandler("PUT", "/*", [this](auto& m, auto& p, auto& b) {
            return HandlePut(m, p, b);
        });
        http_server_.RegisterHandler("DELETE", "/*", [this](auto& m, auto& p, auto& b) {
            return HandleDelete(m, p, b);
        });
        http_server_.RegisterHandler("HEAD", "/*", [this](auto& m, auto& p, auto& b) {
            return HandleHead(m, p, b);
        });
    }

    // Parse S3 path: /{bucket}/{key}
    static bool ParseS3Path(const std::string& path, std::string& bucket, std::string& key) {
        if (path.empty() || path[0] != '/') return false;

        size_t first_slash = path.find('/', 1);
        if (first_slash == std::string::npos) {
            bucket = path.substr(1);
            key.clear();
        } else {
            bucket = path.substr(1, first_slash - 1);
            key = path.substr(first_slash + 1);
        }
        return !bucket.empty();
    }

    // Generate ISO 8601 timestamp
    static std::string FormatTimestamp(uint64_t ts) {
        time_t t = static_cast<time_t>(ts);
        std::tm* tm = std::gmtime(&t);
        std::ostringstream oss;
        oss << std::put_time(tm, "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    }

    // Generate ETag from size and mtime
    static std::string GenerateETag(uint64_t size, uint64_t mtime) {
        std::ostringstream oss;
        oss << "\"" << std::hex << (size ^ mtime) << "\"";
        return oss.str();
    }

    // === Handler implementations ===

    std::string HandleListBuckets(const std::string&, const std::string&, const std::string&) {
        // Return empty bucket list for now
        return R"(<?xml version="1.0" encoding="UTF-8"?>
<ListAllMyBucketsResult xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
  <Owner><ID>owner</ID><DisplayName>owner</DisplayName></Owner>
  <Buckets></Buckets>
</ListAllMyBucketsResult>)";
    }

    std::string HandleGet(const std::string&, const std::string& path, const std::string&) {
        std::string bucket, key;
        if (!ParseS3Path(path, bucket, key)) {
            return ErrorResponse("InvalidURI", "Invalid request URI");
        }

        if (key.empty()) {
            // ListObjects
            return HandleListObjects(bucket);
        }

        // GetObject
        ByteBuffer data;
        auto task = gateway_->GetObject(bucket, key, &data);
        // Synchronous wait (simplified)
        auto status = task.get();

        if (!status.OK()) {
            if (status.code() == ErrorCode::kNotFound) {
                return ErrorResponse("NoSuchKey", "The specified key does not exist.");
            }
            return ErrorResponse("InternalError", status.message());
        }

        return data.ToString();
    }

    std::string HandleListObjects(const std::string& bucket) {
        std::vector<S3Object> objects;
        auto task = gateway_->ListObjects(bucket, "", &objects);
        auto status = task.get();

        if (!status.OK()) {
            if (status.code() == ErrorCode::kNotFound) {
                return ErrorResponse("NoSuchBucket", "The specified bucket does not exist.");
            }
            return ErrorResponse("InternalError", status.message());
        }

        std::ostringstream xml;
        xml << R"(<?xml version="1.0" encoding="UTF-8"?>)"
            << R"(<ListBucketResult xmlns="http://s3.amazonaws.com/doc/2006-03-01/">)"
            << "<Name>" << bucket << "</Name>"
            << "<Prefix></Prefix><MaxKeys>1000</MaxKeys><IsTruncated>false</IsTruncated>";

        for (const auto& obj : objects) {
            xml << "<Contents>"
                << "<Key>" << obj.key << "</Key>"
                << "<Size>" << obj.size << "</Size>"
                << "<LastModified>" << FormatTimestamp(obj.mtime) << "</LastModified>"
                << "<ETag>" << obj.etag << "</ETag>"
                << "</Contents>";
        }
        xml << "</ListBucketResult>";
        return xml.str();
    }

    std::string HandlePut(const std::string&, const std::string& path, const std::string& body) {
        std::string bucket, key;
        if (!ParseS3Path(path, bucket, key)) {
            return ErrorResponse("InvalidURI", "Invalid request URI");
        }

        if (key.empty()) {
            // CreateBucket - simplified, just return success
            return "";
        }

        // PutObject
        ByteBuffer data(body.data(), body.size());
        auto task = gateway_->PutObject(bucket, key, data);
        auto status = task.get();

        if (!status.OK()) {
            return ErrorResponse("InternalError", status.message());
        }
        return "";
    }

    std::string HandleDelete(const std::string&, const std::string& path, const std::string&) {
        std::string bucket, key;
        if (!ParseS3Path(path, bucket, key)) {
            return ErrorResponse("InvalidURI", "Invalid request URI");
        }

        if (key.empty()) {
            // DeleteBucket - simplified
            return "";
        }

        // DeleteObject
        auto task = gateway_->DeleteObject(bucket, key);
        auto status = task.get();

        if (!status.OK() && status.code() != ErrorCode::kNotFound) {
            return ErrorResponse("InternalError", status.message());
        }
        return "";
    }

    std::string HandleHead(const std::string&, const std::string& path, const std::string&) {
        std::string bucket, key;
        if (!ParseS3Path(path, bucket, key)) {
            return ErrorResponse("InvalidURI", "Invalid request URI");
        }

        if (key.empty()) {
            // HeadBucket - simplified
            return "";
        }

        // HeadObject
        InodeAttr attr;
        auto task = gateway_->HeadObject(bucket, key, &attr);
        auto status = task.get();

        if (!status.OK()) {
            if (status.code() == ErrorCode::kNotFound) {
                return ErrorResponse("NoSuchKey", "The specified key does not exist.");
            }
            return ErrorResponse("InternalError", status.message());
        }

        // Return metadata as JSON (headers would be set in real impl)
        std::ostringstream oss;
        oss << R"({"size":)" << attr.size
            << R"(,"mtime":)" << attr.mtime
            << R"(,"etag":)" << GenerateETag(attr.size, attr.mtime) << "}";
        return oss.str();
    }

    static std::string ErrorResponse(const std::string& code, const std::string& message) {
        std::ostringstream xml;
        xml << R"(<?xml version="1.0" encoding="UTF-8"?>)"
            << "<Error><Code>" << code << "</Code>"
            << "<Message>" << message << "</Message></Error>";
        return xml.str();
    }
};

// ================================
// S3Gateway Implementation
// ================================

S3Gateway::S3Gateway(Config config) : config_(std::move(config)) {}

S3Gateway::~S3Gateway() { Stop(); }

Status S3Gateway::Start() {
    if (server_) return Status::Ok();

    server_ = std::make_unique<HTTPServer>(this, config_);
    if (!server_->Start()) {
        server_.reset();
        return Status::IO("Failed to start S3 gateway HTTP server");
    }

    dinfo << "S3Gateway started on " << config_.host << ":" << config_.port << dendl;
    return Status::Ok();
}

void S3Gateway::Stop() {
    if (server_) {
        server_->Stop();
        server_.reset();
        dinfo << "S3Gateway stopped" << dendl;
    }
}

void S3Gateway::Join() {
    // HTTP server runs in background thread, nothing to join here
}

// ================================
// S3 API Operations
// ================================

AsyncTask<Status> S3Gateway::PutObject(
    const std::string& bucket,
    const std::string& key,
    const ByteBuffer& data,
    const std::map<std::string, std::string>& /*metadata*/)
{
    auto ns = config_.namespace_service;
    std::string path = "/" + bucket + "/" + key;
    co_return co_await ns->Write(path, data, 0);
}

AsyncTask<Status> S3Gateway::GetObject(
    const std::string& bucket,
    const std::string& key,
    ByteBuffer* data,
    uint64_t offset,
    uint64_t size)
{
    auto ns = config_.namespace_service;
    std::string path = "/" + bucket + "/" + key;

    if (size == 0) {
        // Get file size first
        InodeAttr attr;
        auto status = co_await ns->GetAttr(path, &attr);
        if (!status.OK()) co_return status;
        size = attr.size - offset;
    }

    co_return co_await ns->Read(path, offset, size, data);
}

AsyncTask<Status> S3Gateway::HeadObject(
    const std::string& bucket,
    const std::string& key,
    InodeAttr* attr)
{
    auto ns = config_.namespace_service;
    std::string path = "/" + bucket + "/" + key;
    co_return co_await ns->GetAttr(path, attr);
}

AsyncTask<Status> S3Gateway::DeleteObject(
    const std::string& bucket,
    const std::string& key)
{
    // Deletion requires metadata service directly
    // For now, return OK (actual impl would call metadata_service->Unlink)
    (void)bucket;
    (void)key;
    co_return Status::Ok();
}

AsyncTask<Status> S3Gateway::ListObjects(
    const std::string& bucket,
    const std::string& prefix,
    std::vector<S3Object>* objects)
{
    auto ns = config_.namespace_service;
    std::string path = "/" + bucket;
    if (!prefix.empty()) {
        path += "/" + prefix;
    }

    std::vector<Dentry> entries;
    auto status = co_await ns->Readdir(path, &entries);
    if (!status.OK()) co_return status;

    objects->clear();
    for (const auto& entry : entries) {
        if (entry.type == FileType::kRegular) {
            InodeAttr attr;
            std::string full_path = path + "/" + entry.name;
            auto attr_status = co_await ns->GetAttr(full_path, &attr);
            if (attr_status.OK()) {
                S3Object obj;
                obj.key = prefix.empty() ? entry.name : prefix + "/" + entry.name;
                obj.size = attr.size;
                obj.mtime = attr.mtime;
                std::ostringstream etag;
                etag << "\"" << std::hex << (attr.size ^ attr.mtime) << "\"";
                obj.etag = etag.str();
                objects->push_back(std::move(obj));
            }
        }
    }

    co_return Status::Ok();
}

AsyncTask<Status> S3Gateway::CreateMultipartUpload(
    const std::string& /*bucket*/,
    const std::string& /*key*/,
    std::string* upload_id)
{
    // Generate upload ID
    *upload_id = std::to_string(NowInMilliSeconds());
    co_return Status::Ok();
}

AsyncTask<Status> S3Gateway::UploadPart(
    const std::string& bucket,
    const std::string& key,
    const std::string& upload_id,
    int part_number,
    const ByteBuffer& data)
{
    // Store part as temporary file
    std::string part_path = "/" + bucket + "/.uploads/" + upload_id + "/" + key + "." + std::to_string(part_number);
    auto ns = config_.namespace_service;
    co_return co_await ns->Write(part_path, data, 0);
}

AsyncTask<Status> S3Gateway::CompleteMultipartUpload(
    const std::string& /*bucket*/,
    const std::string& /*key*/,
    const std::string& /*upload_id*/)
{
    // Merge parts into final object
    // Simplified: actual impl would read all parts and concatenate
    co_return Status::Ok();
}

} // namespace nebulastore::protocol
