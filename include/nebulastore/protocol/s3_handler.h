// S3 操作处理器 (参考 Ceph RGW + 3FS)
#pragma once

#include "nebulastore/protocol/s3_types.h"
#include "nebulastore/protocol/s3_router.h"
#include "nebulastore/protocol/s3_xml.h"
#include "nebulastore/protocol/s3_metadata.h"
#include "nebulastore/protocol/s3_backend_rocksdb.h"
#include <memory>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <functional>
#include <openssl/evp.h>

namespace nebulastore {
namespace s3 {

class S3Handler {
public:
    // 使用抽象后端接口
    S3Handler(void* /*backend*/, const std::string& data_dir,
              const std::string& meta_backend = "rocksdb")
        : data_dir_(data_dir) {
        std::filesystem::create_directories(data_dir_);
        std::filesystem::create_directories(data_dir_ + "/data");

        // 注册并创建后端
        RegisterRocksDBBackend();
        auto backend = MetadataBackendFactory::Instance().Create(
            meta_backend, data_dir_ + "/metadata");
        meta_store_ = std::make_unique<S3MetadataStore>(std::move(backend));
    }

    S3Response Handle(S3Request& req) {
        S3Router::ParseRequest(req);
        
        switch (req.op) {
            case S3Op::LIST_BUCKETS:    return HandleListBuckets(req);
            case S3Op::CREATE_BUCKET:   return HandleCreateBucket(req);
            case S3Op::DELETE_BUCKET:   return HandleDeleteBucket(req);
            case S3Op::HEAD_BUCKET:     return HandleHeadBucket(req);
            case S3Op::LIST_OBJECTS:
            case S3Op::LIST_OBJECTS_V2: return HandleListObjects(req);
            case S3Op::GET_OBJECT:      return HandleGetObject(req);
            case S3Op::PUT_OBJECT:      return HandlePutObject(req);
            case S3Op::DELETE_OBJECT:   return HandleDeleteObject(req);
            case S3Op::HEAD_OBJECT:     return HandleHeadObject(req);
            default:
                S3Response resp;
                resp.SetError(S3Error{501, "NotImplemented", "Not implemented"});
                return resp;
        }
    }

private:
    std::string data_dir_;
    std::unique_ptr<S3MetadataStore> meta_store_;

    std::string DataPath(const std::string& bucket, const std::string& key) {
        return data_dir_ + "/data/" + bucket + "/" + key;
    }

    uint64_t Now() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    std::string RFC822Time(uint64_t ts) {
        auto t = static_cast<time_t>(ts);
        std::ostringstream ss;
        ss << std::put_time(std::gmtime(&t), "%a, %d %b %Y %H:%M:%S GMT");
        return ss.str();
    }

    std::string ISO8601Time(uint64_t ts) {
        auto t = static_cast<time_t>(ts);
        std::ostringstream ss;
        ss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%S.000Z");
        return ss.str();
    }

    std::string MD5(const std::string& data) {
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int digest_len = 0;
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        EVP_DigestInit_ex(ctx, EVP_md5(), nullptr);
        EVP_DigestUpdate(ctx, data.data(), data.size());
        EVP_DigestFinal_ex(ctx, digest, &digest_len);
        EVP_MD_CTX_free(ctx);
        std::ostringstream ss;
        for (unsigned int i = 0; i < digest_len; i++) {
            ss << std::hex << std::setfill('0') << std::setw(2) << (int)digest[i];
        }
        return ss.str();
    }

    // ========== Bucket 操作 ==========

    S3Response HandleListBuckets(S3Request&) {
        S3Response resp;
        auto buckets = meta_store_->ListBuckets();
        std::vector<BucketInfo> bucket_infos;
        for (const auto& b : buckets) {
            bucket_infos.push_back({b.name, ISO8601Time(b.creation_time)});
        }
        resp.body = S3XMLFormatter::ListBucketsResult("owner", "owner", bucket_infos);
        return resp;
    }

    S3Response HandleCreateBucket(S3Request& req) {
        S3Response resp;
        if (meta_store_->BucketExists(req.bucket_name)) {
            resp.SetError(S3Error::BucketAlreadyExists());
            return resp;
        }
        
        BucketMeta meta;
        meta.name = req.bucket_name;
        meta.owner = "owner";
        meta.creation_time = Now();
        meta.object_count = 0;
        meta.total_size = 0;
        meta.region = "default";
        meta.storage_class = "STANDARD";
        
        if (!meta_store_->PutBucket(meta)) {
            resp.SetError(S3Error::InternalError());
            return resp;
        }
        
        // 创建数据目录
        std::filesystem::create_directories(data_dir_ + "/data/" + req.bucket_name);
        resp.body = "";
        return resp;
    }

    S3Response HandleDeleteBucket(S3Request& req) {
        S3Response resp;
        if (!meta_store_->BucketExists(req.bucket_name)) {
            resp.SetError(S3Error::NoSuchBucket());
            return resp;
        }
        
        // 检查是否为空
        auto objects = meta_store_->ListObjects(req.bucket_name, "", "", 1);
        if (!objects.empty()) {
            resp.SetError(S3Error::BucketNotEmpty());
            return resp;
        }
        
        meta_store_->DeleteBucket(req.bucket_name);
        std::filesystem::remove_all(data_dir_ + "/data/" + req.bucket_name);
        resp.status_code = 204;
        resp.body = "";
        return resp;
    }

    S3Response HandleHeadBucket(S3Request& req) {
        S3Response resp;
        if (!meta_store_->BucketExists(req.bucket_name)) {
            resp.SetError(S3Error::NoSuchBucket());
            return resp;
        }
        resp.body = "";
        return resp;
    }

    // ========== Object 操作 ==========

    S3Response HandleListObjects(S3Request& req) {
        S3Response resp;
        if (!meta_store_->BucketExists(req.bucket_name)) {
            resp.SetError(S3Error::NoSuchBucket());
            return resp;
        }
        
        std::string prefix = req.GetParam("prefix");
        std::string marker = req.GetParam("marker");
        std::string max_keys_str = req.GetParam("max-keys");
        int max_keys = max_keys_str.empty() ? 1000 : std::stoi(max_keys_str);
        
        auto objects = meta_store_->ListObjects(req.bucket_name, prefix, marker, max_keys);
        
        ListObjectsResult r;
        r.bucket_name = req.bucket_name;
        r.prefix = prefix;
        r.marker = marker;
        r.delimiter = req.GetParam("delimiter");
        r.max_keys = max_keys;
        r.is_truncated = (int)objects.size() >= max_keys;
        
        for (const auto& obj : objects) {
            r.objects.push_back({obj.key, obj.etag, obj.size, ISO8601Time(obj.last_modified), obj.storage_class});
        }
        
        resp.body = S3XMLFormatter::ListBucketResult(r);
        return resp;
    }

    S3Response HandleGetObject(S3Request& req) {
        S3Response resp;
        ObjectMeta meta;
        if (!meta_store_->GetObject(req.bucket_name, req.object_key, meta)) {
            resp.SetError(S3Error::NoSuchKey());
            return resp;
        }
        
        // 读取数据文件
        std::ifstream f(meta.data_path, std::ios::binary);
        if (!f) {
            resp.SetError(S3Error::NoSuchKey());
            return resp;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        resp.body = ss.str();
        resp.content_type = meta.content_type.empty() ? "application/octet-stream" : meta.content_type;
        resp.headers["Content-Length"] = std::to_string(meta.size);
        resp.headers["ETag"] = "\"" + meta.etag + "\"";
        resp.headers["Last-Modified"] = RFC822Time(meta.last_modified);
        return resp;
    }

    S3Response HandlePutObject(S3Request& req) {
        S3Response resp;
        if (!meta_store_->BucketExists(req.bucket_name)) {
            resp.SetError(S3Error::NoSuchBucket());
            return resp;
        }
        
        // 计算 ETag
        std::string etag = MD5(req.body);
        
        // 写入数据文件
        std::string data_path = DataPath(req.bucket_name, req.object_key);
        std::filesystem::create_directories(std::filesystem::path(data_path).parent_path());
        std::ofstream f(data_path, std::ios::binary);
        f << req.body;
        f.close();
        
        // 检查是否是更新（用于统计）
        bool is_update = meta_store_->ObjectExists(req.bucket_name, req.object_key);
        ObjectMeta old_meta;
        if (is_update) {
            meta_store_->GetObject(req.bucket_name, req.object_key, old_meta);
        }
        
        // 创建元数据
        ObjectMeta meta;
        meta.bucket = req.bucket_name;
        meta.key = req.object_key;
        meta.size = req.body.size();
        meta.etag = etag;
        meta.content_type = req.GetHeader("Content-Type");
        if (meta.content_type.empty()) meta.content_type = "application/octet-stream";
        meta.last_modified = Now();
        meta.storage_class = "STANDARD";
        meta.data_path = data_path;
        
        // 提取用户自定义元数据 (x-amz-meta-*)
        for (const auto& [k, v] : req.headers) {
            if (k.find("x-amz-meta-") == 0 || k.find("X-Amz-Meta-") == 0) {
                meta.user_metadata[k] = v;
            }
        }
        
        if (!meta_store_->PutObject(meta)) {
            resp.SetError(S3Error::InternalError());
            return resp;
        }
        
        // 更新 bucket 统计
        int64_t size_delta = is_update ? (int64_t)meta.size - (int64_t)old_meta.size : (int64_t)meta.size;
        int64_t count_delta = is_update ? 0 : 1;
        meta_store_->UpdateBucketStats(req.bucket_name, size_delta, count_delta);
        
        resp.headers["ETag"] = "\"" + etag + "\"";
        resp.body = "";
        return resp;
    }

    S3Response HandleDeleteObject(S3Request& req) {
        S3Response resp;
        ObjectMeta meta;
        if (meta_store_->GetObject(req.bucket_name, req.object_key, meta)) {
            // 删除数据文件
            std::filesystem::remove(meta.data_path);
            // 删除元数据
            meta_store_->DeleteObject(req.bucket_name, req.object_key);
            // 更新 bucket 统计
            meta_store_->UpdateBucketStats(req.bucket_name, -(int64_t)meta.size, -1);
        }
        resp.status_code = 204;
        resp.body = "";
        return resp;
    }

    S3Response HandleHeadObject(S3Request& req) {
        S3Response resp;
        ObjectMeta meta;
        if (!meta_store_->GetObject(req.bucket_name, req.object_key, meta)) {
            resp.SetError(S3Error::NoSuchKey());
            return resp;
        }
        resp.headers["Content-Length"] = std::to_string(meta.size);
        resp.headers["ETag"] = "\"" + meta.etag + "\"";
        resp.headers["Last-Modified"] = RFC822Time(meta.last_modified);
        resp.headers["Content-Type"] = meta.content_type;
        resp.body = "";
        return resp;
    }
};

} // namespace s3
} // namespace nebulastore
