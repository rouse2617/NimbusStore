// ================================
// RocksDB 存储实现
// 基于 Ceph RGW SAL 设计模式
// ================================

#include "nebulastore/metadata/rocksdb_store.h"
#include "nebulastore/common/logger.h"
#include <cstring>

namespace nebulastore::metadata {

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
    options.IncreaseParallelism(4);

    // 配置缓存
    rocksdb::BlockBasedTableOptions table_options;
    table_options.block_cache = rocksdb::NewLRUCache(config_.cache_size);
    options.table_factory.reset(rocksdb::NewBlockBasedTableFactory(table_options));

    // 打开数据库
    auto status = rocksdb::DB::Open(options, config_.db_path, &db_);
    if (!status.ok()) {
        LOG_ERROR("Failed to open RocksDB: {}", status.ToString());
        return Status::IO("Failed to open RocksDB: " + status.ToString());
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
    auto key = EncodeDentryKey(parent, name);
    std::string value;
    auto status = db_->Get(rocksdb::ReadOptions(), key, &value);

    if (status.IsNotFound()) {
        return Status::NotFound("Dentry not found: " + name);
    }
    if (!status.ok()) {
        LOG_ERROR("Failed to lookup dentry: {}", status.ToString());
        return Status::IO("Failed to lookup dentry: " + status.ToString());
    }

    *dentry = DecodeDentryValue(value);
    return Status::OK();
}

Status RocksDBStore::LookupInode(
    InodeID inode,
    InodeAttr* attr
) {
    auto key = EncodeInodeKey(inode);
    std::string value;
    auto status = db_->Get(rocksdb::ReadOptions(), key, &value);

    if (status.IsNotFound()) {
        return Status::NotFound("Inode not found: " + std::to_string(inode));
    }
    if (!status.ok()) {
        LOG_ERROR("Failed to lookup inode: {}", status.ToString());
        return Status::IO("Failed to lookup inode: " + status.ToString());
    }

    *attr = DecodeInodeValue(value);
    return Status::OK();
}

Status RocksDBStore::LookupLayout(
    InodeID inode,
    FileLayout* layout
) {
    auto key = EncodeLayoutKey(inode);
    std::string value;
    auto status = db_->Get(rocksdb::ReadOptions(), key, &value);

    if (status.IsNotFound()) {
        // 没有布局是正常的（新文件）
        layout->inode_id = inode;
        layout->chunk_size = 4 * 1024 * 1024;  // 默认 4MB
        layout->slices.clear();
        return Status::OK();
    }
    if (!status.ok()) {
        LOG_ERROR("Failed to lookup layout: {}", status.ToString());
        return Status::IO("Failed to lookup layout: " + status.ToString());
    }

    *layout = DecodeLayoutValue(value);
    return Status::OK();
}

// ================================
// Key 编码
// ================================

std::string RocksDBStore::EncodeDentryKey(InodeID parent, const std::string& name) {
    // 格式: "D" + parent(8字节小端) + "/" + name
    std::string key;
    key.reserve(1 + 8 + 1 + name.size());
    key.push_back('D');

    // 小端序写入 parent inode
    for (int i = 0; i < 8; ++i) {
        key.push_back((parent >> (i * 8)) & 0xFF);
    }

    key.push_back('/');
    key.append(name);
    return key;
}

std::string RocksDBStore::EncodeInodeKey(InodeID inode) {
    // 格式: "I" + inode_id(8字节小端)
    std::string key;
    key.reserve(9);
    key.push_back('I');

    for (int i = 0; i < 8; ++i) {
        key.push_back((inode >> (i * 8)) & 0xFF);
    }
    return key;
}

std::string RocksDBStore::EncodeLayoutKey(InodeID inode) {
    // 格式: "L" + inode_id(8字节小端)
    std::string key;
    key.reserve(9);
    key.push_back('L');

    for (int i = 0; i < 8; ++i) {
        key.push_back((inode >> (i * 8)) & 0xFF);
    }
    return key;
}

// ================================
// Value 编码
// ================================

// Dentry Value: inode_id(8) + type(4)
std::string RocksDBStore::EncodeDentryValue(const Dentry& dentry) {
    std::string value;
    value.reserve(12);

    // inode_id (8 bytes, little endian)
    for (int i = 0; i < 8; ++i) {
        value.push_back((dentry.inode_id >> (i * 8)) & 0xFF);
    }

    // type (4 bytes)
    uint32_t type = static_cast<uint32_t>(dentry.type);
    for (int i = 0; i < 4; ++i) {
        value.push_back((type >> (i * 8)) & 0xFF);
    }

    return value;
}

Dentry RocksDBStore::DecodeDentryValue(const std::string& value) {
    Dentry dentry;

    if (value.size() < 12) {
        LOG_ERROR("Invalid dentry value size: {}", value.size());
        return dentry;
    }

    // inode_id
    dentry.inode_id = 0;
    for (int i = 0; i < 8; ++i) {
        dentry.inode_id |= (static_cast<uint8_t>(value[i]) << (i * 8));
    }

    // type
    uint32_t type = 0;
    for (int i = 0; i < 4; ++i) {
        type |= (static_cast<uint8_t>(value[8 + i]) << (i * 8));
    }
    dentry.type = static_cast<FileType>(type);

    return dentry;
}

// Inode Value: inode_id(8) + mode(4) + uid(4) + gid(4) + size(8) + mtime(8) + ctime(8) + nlink(8)
std::string RocksDBStore::EncodeInodeValue(const InodeAttr& inode) {
    std::string value;
    value.reserve(52);

    // inode_id (8 bytes)
    for (int i = 0; i < 8; ++i) {
        value.push_back((inode.inode_id >> (i * 8)) & 0xFF);
    }

    // mode (4 bytes)
    for (int i = 0; i < 4; ++i) {
        value.push_back((inode.mode.mode >> (i * 8)) & 0xFF);
    }

    // uid (4 bytes)
    for (int i = 0; i < 4; ++i) {
        value.push_back((inode.uid >> (i * 8)) & 0xFF);
    }

    // gid (4 bytes)
    for (int i = 0; i < 4; ++i) {
        value.push_back((inode.gid >> (i * 8)) & 0xFF);
    }

    // size (8 bytes)
    for (int i = 0; i < 8; ++i) {
        value.push_back((inode.size >> (i * 8)) & 0xFF);
    }

    // mtime (8 bytes)
    for (int i = 0; i < 8; ++i) {
        value.push_back((inode.mtime >> (i * 8)) & 0xFF);
    }

    // ctime (8 bytes)
    for (int i = 0; i < 8; ++i) {
        value.push_back((inode.ctime >> (i * 8)) & 0xFF);
    }

    // nlink (8 bytes)
    for (int i = 0; i < 8; ++i) {
        value.push_back((inode.nlink >> (i * 8)) & 0xFF);
    }

    return value;
}

InodeAttr RocksDBStore::DecodeInodeValue(const std::string& value) {
    InodeAttr attr{};

    if (value.size() < 52) {
        LOG_ERROR("Invalid inode value size: {}", value.size());
        return attr;
    }

    size_t pos = 0;

    // inode_id
    attr.inode_id = 0;
    for (int i = 0; i < 8; ++i, ++pos) {
        attr.inode_id |= (static_cast<uint8_t>(value[pos]) << (i * 8));
    }

    // mode
    attr.mode.mode = 0;
    for (int i = 0; i < 4; ++i, ++pos) {
        attr.mode.mode |= (static_cast<uint8_t>(value[pos]) << (i * 8));
    }

    // uid
    attr.uid = 0;
    for (int i = 0; i < 4; ++i, ++pos) {
        attr.uid |= (static_cast<uint8_t>(value[pos]) << (i * 8));
    }

    // gid
    attr.gid = 0;
    for (int i = 0; i < 4; ++i, ++pos) {
        attr.gid |= (static_cast<uint8_t>(value[pos]) << (i * 8));
    }

    // size
    attr.size = 0;
    for (int i = 0; i < 8; ++i, ++pos) {
        attr.size |= (static_cast<uint64_t>(value[pos]) << (i * 8));
    }

    // mtime
    attr.mtime = 0;
    for (int i = 0; i < 8; ++i, ++pos) {
        attr.mtime |= (static_cast<uint64_t>(value[pos]) << (i * 8));
    }

    // ctime
    attr.ctime = 0;
    for (int i = 0; i < 8; ++i, ++pos) {
        attr.ctime |= (static_cast<uint64_t>(value[pos]) << (i * 8));
    }

    // nlink
    attr.nlink = 0;
    for (int i = 0; i < 8; ++i, ++pos) {
        attr.nlink |= (static_cast<uint64_t>(value[pos]) << (i * 8));
    }

    return attr;
}

// FileLayout Value: chunk_size(8) + slice_count(4) + [slice_id(8) + offset(8) + size(8) + key_len(4) + key_bytes]*
std::string RocksDBStore::EncodeLayoutValue(const FileLayout& layout) {
    std::string value;

    // chunk_size (8 bytes)
    for (int i = 0; i < 8; ++i) {
        value.push_back((layout.chunk_size >> (i * 8)) & 0xFF);
    }

    // slice_count (4 bytes)
    uint32_t count = static_cast<uint32_t>(layout.slices.size());
    for (int i = 0; i < 4; ++i) {
        value.push_back((count >> (i * 8)) & 0xFF);
    }

    // slices
    for (const auto& slice : layout.slices) {
        // slice_id (8 bytes)
        for (int i = 0; i < 8; ++i) {
            value.push_back((slice.slice_id >> (i * 8)) & 0xFF);
        }
        // offset (8 bytes)
        for (int i = 0; i < 8; ++i) {
            value.push_back((slice.offset >> (i * 8)) & 0xFF);
        }
        // size (8 bytes)
        for (int i = 0; i < 8; ++i) {
            value.push_back((slice.size >> (i * 8)) & 0xFF);
        }
        // key_len (4 bytes)
        uint32_t key_len = static_cast<uint32_t>(slice.storage_key.size());
        for (int i = 0; i < 4; ++i) {
            value.push_back((key_len >> (i * 8)) & 0xFF);
        }
        // key_bytes
        value.append(slice.storage_key);
    }

    return value;
}

FileLayout RocksDBStore::DecodeLayoutValue(const std::string& value) {
    FileLayout layout{};
    layout.slices.clear();

    if (value.size() < 12) {
        return layout;
    }

    size_t pos = 0;

    // chunk_size
    layout.chunk_size = 0;
    for (int i = 0; i < 8; ++i, ++pos) {
        layout.chunk_size |= (static_cast<uint64_t>(value[pos]) << (i * 8));
    }

    // slice_count
    uint32_t count = 0;
    for (int i = 0; i < 4; ++i, ++pos) {
        count |= (static_cast<uint8_t>(value[pos]) << (i * 8));
    }

    // slices
    for (uint32_t s = 0; s < count && pos + 28 <= value.size(); ++s) {
        SliceInfo slice{};

        // slice_id
        slice.slice_id = 0;
        for (int i = 0; i < 8; ++i, ++pos) {
            slice.slice_id |= (static_cast<uint64_t>(value[pos]) << (i * 8));
        }

        // offset
        slice.offset = 0;
        for (int i = 0; i < 8; ++i, ++pos) {
            slice.offset |= (static_cast<uint64_t>(value[pos]) << (i * 8));
        }

        // size
        slice.size = 0;
        for (int i = 0; i < 8; ++i, ++pos) {
            slice.size |= (static_cast<uint64_t>(value[pos]) << (i * 8));
        }

        // key_len
        uint32_t key_len = 0;
        for (int i = 0; i < 4; ++i, ++pos) {
            key_len |= (static_cast<uint8_t>(value[pos]) << (i * 8));
        }

        // key_bytes
        if (pos + key_len <= value.size()) {
            slice.storage_key = value.substr(pos, key_len);
            pos += key_len;
            layout.slices.push_back(slice);
        } else {
            break;
        }
    }

    return layout;
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
        return Status::IO("Transaction commit failed: " + status.ToString());
    }

    committed_ = true;
    return Status::OK();
}

Status RocksDBTransaction::Rollback() {
    batch_.Clear();
    committed_ = true;  // 标记为已提交，避免析构时再次回滚
    return Status::OK();
}

} // namespace nebulastore::metadata
