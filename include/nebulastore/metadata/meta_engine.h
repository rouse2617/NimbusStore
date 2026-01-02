#pragma once

#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>
#include "nebulastore/common/types.h"

namespace nebulastore::metadata {

// ================================
// KV 操作类型 (用于事务)
// ================================
enum class TxnOpType { kPut, kDelete };

struct TxnOp {
    TxnOpType type;
    std::string key;
    std::string value;  // Delete 时为空
};

// ================================
// Scan 结果
// ================================
struct KVPair {
    std::string key;
    std::string value;
};

// ================================
// KVClient 接口 - 通用 KV 存储抽象
// ================================
class KVClient {
public:
    virtual ~KVClient() = default;

    // 基础操作
    virtual Status Get(const std::string& key, std::string* value) = 0;
    virtual Status Set(const std::string& key, const std::string& value) = 0;
    virtual Status Delete(const std::string& key) = 0;

    // 范围扫描: [start, end)
    virtual Status Scan(const std::string& start, const std::string& end,
                        std::vector<KVPair>* results, uint32_t limit = 0) = 0;

    // 事务批量操作
    virtual Status Txn(const std::vector<TxnOp>& ops) = 0;
};

// ================================
// KV 驱动工厂函数类型
// ================================
struct KVConfig {
    std::string type;  // "rocksdb", "tikv", "etcd"
    std::string path;  // 本地路径或远程地址
    std::unordered_map<std::string, std::string> options;
};

using KVDriverFactory = std::function<std::unique_ptr<KVClient>(const KVConfig&)>;

// ================================
// KV 驱动注册表
// ================================
class KVRegistry {
public:
    static KVRegistry& Instance() {
        static KVRegistry instance;
        return instance;
    }

    void RegisterDriver(const std::string& name, KVDriverFactory factory) {
        std::lock_guard<std::mutex> lock(mutex_);
        drivers_[name] = std::move(factory);
    }

    std::unique_ptr<KVClient> NewClient(const KVConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = drivers_.find(config.type);
        if (it == drivers_.end()) return nullptr;
        return it->second(config);
    }

private:
    KVRegistry() = default;
    std::mutex mutex_;
    std::unordered_map<std::string, KVDriverFactory> drivers_;
};

// 便捷函数
inline void RegisterKVDriver(const std::string& name, KVDriverFactory factory) {
    KVRegistry::Instance().RegisterDriver(name, std::move(factory));
}

inline std::unique_ptr<KVClient> NewKVClient(const KVConfig& config) {
    return KVRegistry::Instance().NewClient(config);
}

// ================================
// MetaEngine 接口 - 元数据引擎抽象
// ================================
class MetaEngine {
public:
    virtual ~MetaEngine() = default;

    // Inode 操作
    virtual Status CreateInode(InodeID inode, FileMode mode, UserID uid, GroupID gid) = 0;
    virtual Status GetAttr(InodeID inode, InodeAttr* attr) = 0;
    virtual Status SetAttr(InodeID inode, const InodeAttr& attr, uint32_t to_set) = 0;
    virtual Status DeleteInode(InodeID inode) = 0;

    // Dentry 操作
    virtual Status Lookup(InodeID parent, const std::string& name, Dentry* dentry) = 0;
    virtual Status CreateDentry(InodeID parent, const std::string& name, InodeID inode, FileType type) = 0;
    virtual Status DeleteDentry(InodeID parent, const std::string& name) = 0;
    virtual Status Readdir(InodeID parent, std::vector<Dentry>* entries) = 0;

    // Slice 操作
    virtual Status AddSlice(InodeID inode, const SliceInfo& slice) = 0;
    virtual Status GetLayout(InodeID inode, FileLayout* layout) = 0;

    // ID 生成
    virtual InodeID AllocateInodeID() = 0;
};

// ================================
// 基于 KVClient 的 MetaEngine 实现
// ================================
class KVMetaEngine : public MetaEngine {
public:
    explicit KVMetaEngine(std::unique_ptr<KVClient> client)
        : client_(std::move(client)) {}

    Status CreateInode(InodeID inode, FileMode mode, UserID uid, GroupID gid) override {
        InodeAttr attr{inode, mode, uid, gid, 0, NowInSeconds(), NowInSeconds(), 1};
        return client_->Set(EncodeInodeKey(inode), EncodeInodeValue(attr));
    }

    Status GetAttr(InodeID inode, InodeAttr* attr) override {
        std::string value;
        auto s = client_->Get(EncodeInodeKey(inode), &value);
        if (!s.OK()) return s;
        *attr = DecodeInodeValue(value);
        return Status::Ok();
    }

    Status SetAttr(InodeID inode, const InodeAttr& attr, uint32_t /*to_set*/) override {
        return client_->Set(EncodeInodeKey(inode), EncodeInodeValue(attr));
    }

    Status DeleteInode(InodeID inode) override {
        return client_->Delete(EncodeInodeKey(inode));
    }

    Status Lookup(InodeID parent, const std::string& name, Dentry* dentry) override {
        std::string value;
        auto s = client_->Get(EncodeDentryKey(parent, name), &value);
        if (!s.OK()) return s;
        *dentry = DecodeDentryValue(value);
        dentry->name = name;
        return Status::Ok();
    }

    Status CreateDentry(InodeID parent, const std::string& name, InodeID inode, FileType type) override {
        Dentry d{name, inode, type};
        return client_->Set(EncodeDentryKey(parent, name), EncodeDentryValue(d));
    }

    Status DeleteDentry(InodeID parent, const std::string& name) override {
        return client_->Delete(EncodeDentryKey(parent, name));
    }

    Status Readdir(InodeID parent, std::vector<Dentry>* entries) override {
        std::string prefix = "D" + EncodeU64(parent);
        std::string end = "D" + EncodeU64(parent + 1);
        std::vector<KVPair> kvs;
        auto s = client_->Scan(prefix, end, &kvs);
        if (!s.OK()) return s;
        entries->clear();
        for (auto& kv : kvs) {
            Dentry d = DecodeDentryValue(kv.value);
            d.name = kv.key.substr(9);  // 跳过 "D" + 8字节 parent
            entries->push_back(std::move(d));
        }
        return Status::Ok();
    }

    Status AddSlice(InodeID inode, const SliceInfo& slice) override {
        std::string key = EncodeSliceKey(inode, slice.offset);
        return client_->Set(key, EncodeSliceValue(slice));
    }

    Status GetLayout(InodeID inode, FileLayout* layout) override {
        std::string prefix = "S" + EncodeU64(inode);
        std::string end = "S" + EncodeU64(inode + 1);
        std::vector<KVPair> kvs;
        auto s = client_->Scan(prefix, end, &kvs);
        if (!s.OK()) return s;
        layout->inode_id = inode;
        layout->chunk_size = 4 * 1024 * 1024;  // 4MB default
        layout->slices.clear();
        for (auto& kv : kvs) {
            layout->slices.push_back(DecodeSliceValue(kv.value));
        }
        return Status::Ok();
    }

    InodeID AllocateInodeID() override {
        return next_inode_.fetch_add(1);
    }

private:
    std::unique_ptr<KVClient> client_;
    std::atomic<InodeID> next_inode_{2};  // 1 = root

    // Key 编码
    static std::string EncodeU64(uint64_t v) {
        std::string s(8, '\0');
        for (int i = 7; i >= 0; --i) { s[i] = v & 0xFF; v >>= 8; }
        return s;
    }

    static uint64_t DecodeU64(const std::string& s, size_t off = 0) {
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v = (v << 8) | static_cast<uint8_t>(s[off + i]);
        return v;
    }

    static std::string EncodeInodeKey(InodeID inode) { return "I" + EncodeU64(inode); }
    static std::string EncodeDentryKey(InodeID parent, const std::string& name) {
        return "D" + EncodeU64(parent) + name;
    }
    static std::string EncodeSliceKey(InodeID inode, uint64_t offset) {
        return "S" + EncodeU64(inode) + EncodeU64(offset);
    }

    // Value 编码 (简单二进制格式)
    static std::string EncodeInodeValue(const InodeAttr& a) {
        std::string s;
        s.reserve(64);
        s += EncodeU64(a.inode_id);
        s += EncodeU64(a.mode.mode);
        s += EncodeU64(a.uid);
        s += EncodeU64(a.gid);
        s += EncodeU64(a.size);
        s += EncodeU64(a.mtime);
        s += EncodeU64(a.ctime);
        s += EncodeU64(a.nlink);
        return s;
    }

    static InodeAttr DecodeInodeValue(const std::string& s) {
        InodeAttr a;
        a.inode_id = DecodeU64(s, 0);
        a.mode = FileMode::FromUint(static_cast<uint32_t>(DecodeU64(s, 8)));
        a.uid = static_cast<UserID>(DecodeU64(s, 16));
        a.gid = static_cast<GroupID>(DecodeU64(s, 24));
        a.size = DecodeU64(s, 32);
        a.mtime = DecodeU64(s, 40);
        a.ctime = DecodeU64(s, 48);
        a.nlink = DecodeU64(s, 56);
        return a;
    }

    static std::string EncodeDentryValue(const Dentry& d) {
        std::string s;
        s += EncodeU64(d.inode_id);
        s += EncodeU64(static_cast<uint64_t>(d.type));
        return s;
    }

    static Dentry DecodeDentryValue(const std::string& s) {
        Dentry d;
        d.inode_id = DecodeU64(s, 0);
        d.type = static_cast<FileType>(DecodeU64(s, 8));
        return d;
    }

    static std::string EncodeSliceValue(const SliceInfo& sl) {
        std::string s;
        s += EncodeU64(sl.slice_id);
        s += EncodeU64(sl.offset);
        s += EncodeU64(sl.size);
        s += EncodeU64(sl.storage_key.size());
        s += sl.storage_key;
        return s;
    }

    static SliceInfo DecodeSliceValue(const std::string& s) {
        SliceInfo sl;
        sl.slice_id = DecodeU64(s, 0);
        sl.offset = DecodeU64(s, 8);
        sl.size = DecodeU64(s, 16);
        uint64_t len = DecodeU64(s, 24);
        sl.storage_key = s.substr(32, len);
        return sl;
    }
};

// ================================
// RocksDB KVClient 适配器
// ================================
class RocksDBKVClient : public KVClient {
public:
    explicit RocksDBKVClient(rocksdb::DB* db) : db_(db) {}

    Status Get(const std::string& key, std::string* value) override {
        auto s = db_->Get(rocksdb::ReadOptions(), key, value);
        if (s.IsNotFound()) return Status::NotFound();
        if (!s.ok()) return Status::IO(s.ToString());
        return Status::Ok();
    }

    Status Set(const std::string& key, const std::string& value) override {
        auto s = db_->Put(rocksdb::WriteOptions(), key, value);
        if (!s.ok()) return Status::IO(s.ToString());
        return Status::Ok();
    }

    Status Delete(const std::string& key) override {
        auto s = db_->Delete(rocksdb::WriteOptions(), key);
        if (!s.ok()) return Status::IO(s.ToString());
        return Status::Ok();
    }

    Status Scan(const std::string& start, const std::string& end,
                std::vector<KVPair>* results, uint32_t limit = 0) override {
        results->clear();
        std::unique_ptr<rocksdb::Iterator> it(db_->NewIterator(rocksdb::ReadOptions()));
        uint32_t count = 0;
        for (it->Seek(start); it->Valid() && it->key().ToString() < end; it->Next()) {
            results->push_back({it->key().ToString(), it->value().ToString()});
            if (limit > 0 && ++count >= limit) break;
        }
        return Status::Ok();
    }

    Status Txn(const std::vector<TxnOp>& ops) override {
        rocksdb::WriteBatch batch;
        for (auto& op : ops) {
            if (op.type == TxnOpType::kPut) batch.Put(op.key, op.value);
            else batch.Delete(op.key);
        }
        auto s = db_->Write(rocksdb::WriteOptions(), &batch);
        if (!s.ok()) return Status::IO(s.ToString());
        return Status::Ok();
    }

private:
    rocksdb::DB* db_;
};

} // namespace nebulastore::metadata
