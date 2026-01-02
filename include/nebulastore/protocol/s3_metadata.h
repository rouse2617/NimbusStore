// S3 元数据存储 - 抽象后端接口
// 支持 RocksDB, Redis 等多种后端
#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>
#include <cstring>
#include <functional>

namespace nebulastore {
namespace s3 {

// ================================
// 元数据结构
// ================================
struct BucketMeta {
    std::string name;
    std::string owner;
    uint64_t creation_time = 0;
    uint64_t object_count = 0;
    uint64_t total_size = 0;
    std::string region;
    std::string storage_class;

    std::string Encode() const;
    bool Decode(const std::string& data);
};

struct ObjectMeta {
    std::string bucket;
    std::string key;
    uint64_t size = 0;
    std::string etag;
    std::string content_type;
    uint64_t last_modified = 0;
    std::string storage_class;
    std::string data_path;
    std::map<std::string, std::string> user_metadata;

    std::string Encode() const;
    bool Decode(const std::string& data);
};

// ================================
// 抽象后端接口
// ================================
class MetadataBackend {
public:
    virtual ~MetadataBackend() = default;

    // KV 操作
    virtual bool Put(const std::string& key, const std::string& value) = 0;
    virtual bool Get(const std::string& key, std::string& value) = 0;
    virtual bool Delete(const std::string& key) = 0;
    virtual bool Exists(const std::string& key) = 0;

    // 批量写入
    virtual bool BatchPut(const std::vector<std::pair<std::string, std::string>>& kvs) = 0;

    // 前缀扫描
    virtual std::vector<std::pair<std::string, std::string>> Scan(
        const std::string& prefix, int limit = 1000) = 0;
};

// ================================
// S3 元数据存储 (使用抽象后端)
// ================================
class S3MetadataStore {
public:
    explicit S3MetadataStore(std::unique_ptr<MetadataBackend> backend)
        : backend_(std::move(backend)) {}

    // Bucket 操作
    bool PutBucket(const BucketMeta& meta);
    bool GetBucket(const std::string& name, BucketMeta& meta);
    bool DeleteBucket(const std::string& name);
    bool BucketExists(const std::string& name);
    std::vector<BucketMeta> ListBuckets();

    // Object 操作
    bool PutObject(const ObjectMeta& meta);
    bool GetObject(const std::string& bucket, const std::string& key, ObjectMeta& meta);
    bool DeleteObject(const std::string& bucket, const std::string& key);
    bool ObjectExists(const std::string& bucket, const std::string& key);
    std::vector<ObjectMeta> ListObjects(const std::string& bucket,
                                        const std::string& prefix = "",
                                        const std::string& marker = "",
                                        int max_keys = 1000);

    bool UpdateBucketStats(const std::string& bucket, int64_t size_delta, int64_t count_delta);

private:
    std::unique_ptr<MetadataBackend> backend_;

    // Key 生成
    static std::string BucketKey(const std::string& name) { return "B:" + name; }
    static std::string BucketListKey(const std::string& name) { return "BL:" + name; }
    static std::string ObjectKey(const std::string& bucket, const std::string& key) {
        return "O:" + bucket + "/" + key;
    }
    static std::string ObjectListKey(const std::string& bucket, const std::string& key) {
        return "OL:" + bucket + "/" + key;
    }
};

// ================================
// 后端工厂
// ================================
class MetadataBackendFactory {
public:
    using Creator = std::function<std::unique_ptr<MetadataBackend>(const std::string&)>;

    static MetadataBackendFactory& Instance() {
        static MetadataBackendFactory instance;
        return instance;
    }

    void Register(const std::string& type, Creator creator) {
        creators_[type] = std::move(creator);
    }

    std::unique_ptr<MetadataBackend> Create(const std::string& type, const std::string& config) {
        auto it = creators_.find(type);
        if (it != creators_.end()) {
            return it->second(config);
        }
        return nullptr;
    }

private:
    std::map<std::string, Creator> creators_;
};

// ================================
// 编码辅助函数
// ================================
namespace encoding {
    inline void PutU32(std::string& buf, uint32_t v) {
        buf.append(reinterpret_cast<const char*>(&v), sizeof(v));
    }
    inline void PutU64(std::string& buf, uint64_t v) {
        buf.append(reinterpret_cast<const char*>(&v), sizeof(v));
    }
    inline void PutString(std::string& buf, const std::string& s) {
        PutU32(buf, s.size());
        buf.append(s);
    }
    inline bool GetU32(const std::string& data, size_t& pos, uint32_t& v) {
        if (pos + 4 > data.size()) return false;
        memcpy(&v, data.data() + pos, 4);
        pos += 4;
        return true;
    }
    inline bool GetU64(const std::string& data, size_t& pos, uint64_t& v) {
        if (pos + 8 > data.size()) return false;
        memcpy(&v, data.data() + pos, 8);
        pos += 8;
        return true;
    }
    inline bool GetString(const std::string& data, size_t& pos, std::string& s) {
        uint32_t len;
        if (!GetU32(data, pos, len)) return false;
        if (pos + len > data.size()) return false;
        s = data.substr(pos, len);
        pos += len;
        return true;
    }
}

// ================================
// BucketMeta 序列化实现
// ================================
inline std::string BucketMeta::Encode() const {
    std::string buf;
    encoding::PutU32(buf, 1);  // version
    encoding::PutString(buf, name);
    encoding::PutString(buf, owner);
    encoding::PutU64(buf, creation_time);
    encoding::PutU64(buf, object_count);
    encoding::PutU64(buf, total_size);
    encoding::PutString(buf, region);
    encoding::PutString(buf, storage_class);
    return buf;
}

inline bool BucketMeta::Decode(const std::string& data) {
    size_t pos = 0;
    uint32_t ver;
    if (!encoding::GetU32(data, pos, ver) || ver > 1) return false;
    return encoding::GetString(data, pos, name) &&
           encoding::GetString(data, pos, owner) &&
           encoding::GetU64(data, pos, creation_time) &&
           encoding::GetU64(data, pos, object_count) &&
           encoding::GetU64(data, pos, total_size) &&
           encoding::GetString(data, pos, region) &&
           encoding::GetString(data, pos, storage_class);
}

// ================================
// ObjectMeta 序列化实现
// ================================
inline std::string ObjectMeta::Encode() const {
    std::string buf;
    encoding::PutU32(buf, 1);  // version
    encoding::PutString(buf, bucket);
    encoding::PutString(buf, key);
    encoding::PutU64(buf, size);
    encoding::PutString(buf, etag);
    encoding::PutString(buf, content_type);
    encoding::PutU64(buf, last_modified);
    encoding::PutString(buf, storage_class);
    encoding::PutString(buf, data_path);
    encoding::PutU32(buf, user_metadata.size());
    for (const auto& [k, v] : user_metadata) {
        encoding::PutString(buf, k);
        encoding::PutString(buf, v);
    }
    return buf;
}

inline bool ObjectMeta::Decode(const std::string& data) {
    size_t pos = 0;
    uint32_t ver;
    if (!encoding::GetU32(data, pos, ver) || ver > 1) return false;
    if (!encoding::GetString(data, pos, bucket) ||
        !encoding::GetString(data, pos, key) ||
        !encoding::GetU64(data, pos, size) ||
        !encoding::GetString(data, pos, etag) ||
        !encoding::GetString(data, pos, content_type) ||
        !encoding::GetU64(data, pos, last_modified) ||
        !encoding::GetString(data, pos, storage_class) ||
        !encoding::GetString(data, pos, data_path)) return false;

    uint32_t count;
    if (!encoding::GetU32(data, pos, count)) return false;
    user_metadata.clear();
    for (uint32_t i = 0; i < count; i++) {
        std::string k, v;
        if (!encoding::GetString(data, pos, k) || !encoding::GetString(data, pos, v)) return false;
        user_metadata[k] = v;
    }
    return true;
}

// ================================
// S3MetadataStore 实现
// ================================
inline bool S3MetadataStore::PutBucket(const BucketMeta& meta) {
    return backend_->BatchPut({
        {BucketKey(meta.name), meta.Encode()},
        {BucketListKey(meta.name), ""}
    });
}

inline bool S3MetadataStore::GetBucket(const std::string& name, BucketMeta& meta) {
    std::string value;
    if (!backend_->Get(BucketKey(name), value)) return false;
    return meta.Decode(value);
}

inline bool S3MetadataStore::DeleteBucket(const std::string& name) {
    return backend_->Delete(BucketKey(name)) && backend_->Delete(BucketListKey(name));
}

inline bool S3MetadataStore::BucketExists(const std::string& name) {
    return backend_->Exists(BucketKey(name));
}

inline std::vector<BucketMeta> S3MetadataStore::ListBuckets() {
    std::vector<BucketMeta> buckets;
    auto kvs = backend_->Scan("BL:");
    for (const auto& [key, _] : kvs) {
        std::string name = key.substr(3);  // 去掉 "BL:"
        BucketMeta meta;
        if (GetBucket(name, meta)) {
            buckets.push_back(meta);
        }
    }
    return buckets;
}

inline bool S3MetadataStore::PutObject(const ObjectMeta& meta) {
    return backend_->BatchPut({
        {ObjectKey(meta.bucket, meta.key), meta.Encode()},
        {ObjectListKey(meta.bucket, meta.key), ""}
    });
}

inline bool S3MetadataStore::GetObject(const std::string& bucket, const std::string& key, ObjectMeta& meta) {
    std::string value;
    if (!backend_->Get(ObjectKey(bucket, key), value)) return false;
    return meta.Decode(value);
}

inline bool S3MetadataStore::DeleteObject(const std::string& bucket, const std::string& key) {
    return backend_->Delete(ObjectKey(bucket, key)) && backend_->Delete(ObjectListKey(bucket, key));
}

inline bool S3MetadataStore::ObjectExists(const std::string& bucket, const std::string& key) {
    return backend_->Exists(ObjectKey(bucket, key));
}

inline std::vector<ObjectMeta> S3MetadataStore::ListObjects(
    const std::string& bucket, const std::string& prefix,
    const std::string& marker, int max_keys) {
    std::vector<ObjectMeta> objects;
    std::string scan_prefix = "OL:" + bucket + "/";
    auto kvs = backend_->Scan(scan_prefix, max_keys * 2);

    for (const auto& [key, _] : kvs) {
        std::string obj_key = key.substr(scan_prefix.size());
        if (!marker.empty() && obj_key <= marker) continue;
        if (!prefix.empty() && obj_key.find(prefix) != 0) continue;

        ObjectMeta meta;
        if (GetObject(bucket, obj_key, meta)) {
            objects.push_back(meta);
            if ((int)objects.size() >= max_keys) break;
        }
    }
    return objects;
}

inline bool S3MetadataStore::UpdateBucketStats(const std::string& bucket, int64_t size_delta, int64_t count_delta) {
    BucketMeta meta;
    if (!GetBucket(bucket, meta)) return false;
    meta.total_size += size_delta;
    meta.object_count += count_delta;
    return PutBucket(meta);
}

} // namespace s3
} // namespace nebulastore
