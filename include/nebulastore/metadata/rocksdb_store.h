#pragma once

#include <rocksdb/db.h>
#include <rocksdb/write_batch.h>
#include <rocksdb/options.h>
#include <memory>
#include "aifs/metadata/metadata_service.h"

namespace nebulastore::metadata {

// ================================
// RocksDB 元数据存储实现
// ================================

class RocksDBStore : public MetadataStore {
public:
    struct Config {
        std::string db_path;
        bool create_if_missing = true;
        uint64_t cache_size = 1ULL << 30;  // 1GB 缓存
        uint32_t max_open_files = 100000;
    };

    explicit RocksDBStore(const Config& config);
    ~RocksDBStore() override;

    Status Init();

    // === 实现 MetadataStore 接口 ===

    std::unique_ptr<Transaction> BeginTransaction() override;

    Status LookupDentry(
        InodeID parent,
        const std::string& name,
        Dentry* dentry
    ) override;

    Status LookupInode(
        InodeID inode,
        InodeAttr* attr
    ) override;

    Status LookupLayout(
        InodeID inode,
        FileLayout* layout
    ) override;

private:
    Config config_;
    rocksdb::DB* db_;
    rocksdb::Options options_;

    // === Key 编码 ===

    // dentry key: "D" + parent_inode(8字节) + name
    std::string EncodeDentryKey(InodeID parent, const std::string& name);

    // inode key: "I" + inode_id(8字节)
    std::string EncodeInodeKey(InodeID inode);

    // layout key: "L" + inode_id(8字节)
    std::string EncodeLayoutKey(InodeID inode);

    // === Value 编码 ===

    std::string EncodeDentryValue(const Dentry& dentry);
    Dentry DecodeDentryValue(const std::string& value);

    std::string EncodeInodeValue(const InodeAttr& inode);
    InodeAttr DecodeInodeValue(const std::string& value);

    std::string EncodeLayoutValue(const FileLayout& layout);
    FileLayout DecodeLayoutValue(const std::string& value);
};

// ================================
// RocksDB 事务实现
// ================================

class RocksDBTransaction : public MetadataStore::Transaction {
public:
    explicit RocksDBTransaction(rocksdb::DB* db, RocksDBStore* store);
    ~RocksDBTransaction() override;

    // === 实现 Transaction 接口 ===

    Status CreateDentry(
        InodeID parent,
        const std::string& name,
        InodeID inode,
        FileType type
    ) override;

    Status CreateInode(
        InodeID inode,
        FileMode mode,
        UserID uid,
        GroupID gid
    ) override;

    Status Commit() override;
    Status Rollback() override;

private:
    rocksdb::DB* db_;
    RocksDBStore* store_;
    rocksdb::WriteBatch batch_;
    bool committed_;

    // 编码后的数据
    std::string dentry_key_;
    std::string dentry_value_;
    std::string inode_key_;
    std::string inode_value_;
};

} // namespace nebulastore::metadata
