// ================================
// RocksDB 存储实现骨架 - 等你来完善
// ================================

#include "nebulastore/metadata/rocksdb_store.h"
#include "nebulastore/common/logger.h"

namespace yig::metadata {

// ================================
// RocksDBStore
// ================================

RocksDBStore::RocksDBStore(const Config& config)
    : config_(config), db_(nullptr) {}

RocksDBStore::~RocksDBStore() {
    if (db_) {
        delete db_;
        db_ = nullptr;
    }
}

Status RocksDBStore::Init() {
    rocksdb::Options options;
    options.create_if_missing = config_.create_if_missing;
    options.OptimizeLevelStyleCompaction();
    options.IncreaseParallelism(4);  // 并行压缩

    // 打开数据库
    auto status = rocksdb::DB::Open(options, config_.db_path, &db_);
    if (!status.ok()) {
        LOG_ERROR("Failed to open RocksDB: {}", status.ToString());
        return Status::IO("Failed to open RocksDB");
    }

    options_ = options;
    LOG_INFO("RocksDB initialized: {}", config_.db_path);
    return Status::OK();
}

std::unique_ptr<MetadataStore::Transaction>
RocksDBStore::BeginTransaction() {
    return std::make_unique<RocksDBTransaction>(db_, this);
}

Status RocksDBStore::LookupDentry(
    InodeID parent,
    const std::string& name,
    Dentry* dentry
) {
    // TODO: 实现查询逻辑
    // auto key = EncodeDentryKey(parent, name);
    // std::string value;
    // auto status = db_->Get(rocksdb::ReadOptions(), key, &value);
    // ...
    return Status::OK();
}

Status RocksDBStore::LookupInode(
    InodeID inode,
    InodeAttr* attr
) {
    // TODO: 实现查询逻辑
    return Status::OK();
}

Status RocksDBStore::LookupLayout(
    InodeID inode,
    FileLayout* layout
) {
    // TODO: 实现查询逻辑
    return Status::OK();
}

// ================================
// Key 编码
// ================================

std::string RocksDBStore::EncodeDentryKey(InodeID parent, const std::string& name) {
    // 格式: "D" + parent(8字节小端) + name
    std::string key;
    key.reserve(1 + 8 + name.size());
    key.push_back('D');
    key.append(reinterpret_cast<const char*>(&parent), 8);
    key.append(name);
    return key;
}

std::string RocksDBStore::EncodeInodeKey(InodeID inode) {
    // 格式: "I" + inode_id(8字节小端)
    std::string key;
    key.reserve(9);
    key.push_back('I');
    key.append(reinterpret_cast<const char*>(&inode), 8);
    return key;
}

std::string RocksDBStore::EncodeLayoutKey(InodeID inode) {
    // 格式: "L" + inode_id(8字节小端)
    std::string key;
    key.reserve(9);
    key.push_back('L');
    key.append(reinterpret_cast<const char*>(&inode), 8);
    return key;
}

// ================================
// Value 编码
// ================================

std::string RocksDBStore::EncodeDentryValue(const Dentry& dentry) {
    // TODO: 使用 FlatBuffers 或 protobuf
    return "";
}

Dentry RocksDBStore::DecodeDentryValue(const std::string& value) {
    // TODO
    return Dentry{};
}

std::string RocksDBStore::EncodeInodeValue(const InodeAttr& inode) {
    // TODO
    return "";
}

InodeAttr RocksDBStore::DecodeInodeValue(const std::string& value) {
    // TODO
    return InodeAttr{};
}

std::string RocksDBStore::EncodeLayoutValue(const FileLayout& layout) {
    // TODO
    return "";
}

FileLayout RocksDBStore::DecodeLayoutValue(const std::string& value) {
    // TODO
    return FileLayout{};
}

// ================================
// RocksDBTransaction
// ================================

RocksDBTransaction::RocksDBTransaction(rocksdb::DB* db, RocksDBStore* store)
    : db_(db), store_(store), committed_(false) {}

RocksDBTransaction::~RocksDBTransaction() {
    if (!committed_) {
        Rollback();
    }
}

Status RocksDBTransaction::CreateDentry(
    InodeID parent,
    const std::string& name,
    InodeID inode,
    FileType type
) {
    // 编码 key 和 value
    dentry_key_ = store_->EncodeDentryKey(parent, name);

    Dentry dentry{name, inode, type};
    dentry_value_ = store_->EncodeDentryValue(dentry);

    // 添加到批处理
    batch_.Put(dentry_key_, dentry_value_);
    return Status::OK();
}

Status RocksDBTransaction::CreateInode(
    InodeID inode,
    FileMode mode,
    UserID uid,
    GroupID gid
) {
    // 编码 key 和 value
    inode_key_ = store_->EncodeInodeKey(inode);

    InodeAttr attr{
        .inode_id = inode,
        .mode = mode,
        .uid = uid,
        .gid = gid,
        .size = 0,
        .mtime = NowInSeconds(),
        .ctime = NowInSeconds(),
        .nlink = 1,
    };
    inode_value_ = store_->EncodeInodeValue(attr);

    // 添加到批处理
    batch_.Put(inode_key_, inode_value_);
    return Status::OK();
}

Status RocksDBTransaction::Commit() {
    rocksdb::WriteOptions write_options;
    write_options.sync = true;  // WAL 同步

    auto status = db_->Write(write_options, &batch_);
    if (!status.ok()) {
        LOG_ERROR("Failed to commit transaction: {}", status.ToString());
        return Status::IO("Transaction commit failed");
    }

    committed_ = true;
    return Status::OK();
}

Status RocksDBTransaction::Rollback() {
    batch_.Clear();
    committed_ = true;  // 标记为已提交，避免析构时再次回滚
    return Status::OK();
}

} // namespace yig::metadata
