// ================================
// 元数据分区实现
// 基于 CubeFS + DeepSeek 3FS 设计
// ================================

#include "nebulastore/metadata/metadata_service.h"
#include "nebulastore/metadata/rocksdb_store.h"
#include "nebulastore/common/logger.h"

namespace nebulastore::metadata {

// ================================
// MetaPartition
// ================================

MetaPartition::MetaPartition(const Config& config)
    : config_(config),
      mode_(ScaleMode::kStandalone) {}

MetaPartition::~MetaPartition() = default;

Status MetaPartition::Init() {
    auto store = std::make_unique<RocksDBStore>(
        RocksDBStore::Config{config_.data_dir}
    );
    auto status = store->Init();
    if (!status.OK()) {
        return status;
    }

    store_ = std::move(store);

    LOG_INFO("MetaPartition initialized: range [%lu, %lu)",
             config_.start_inode, config_.end_inode);
    return Status::Ok();
}

AsyncTask<Status> MetaPartition::Lookup(
    InodeID inode_id,
    InodeAttr* attr
) {
    if (!store_) {
        co_return Status::IO("Store not initialized");
    }
    co_return store_->LookupInode(inode_id, attr);
}

AsyncTask<Status> MetaPartition::LookupDentry(
    InodeID parent,
    const std::string& name,
    Dentry* dentry
) {
    if (!store_) {
        co_return Status::IO("Store not initialized");
    }
    co_return store_->LookupDentry(parent, name, dentry);
}

AsyncTask<Status> MetaPartition::CreateDentry(
    InodeID parent,
    const std::string& name,
    InodeID inode,
    FileType type
) {
    if (!store_) {
        co_return Status::IO("Store not initialized");
    }

    auto txn = store_->BeginTransaction();
    auto status = txn->CreateDentry(parent, name, inode, type);
    if (!status.OK()) {
        co_return status;
    }
    co_return txn->Commit();
}

AsyncTask<Status> MetaPartition::CreateInode(
    InodeID inode,
    FileMode mode,
    UserID uid,
    GroupID gid
) {
    if (!store_) {
        co_return Status::IO("Store not initialized");
    }

    if (inode < config_.start_inode || inode >= config_.end_inode) {
        co_return Status::InvalidArgument("Inode ID out of range");
    }

    auto txn = store_->BeginTransaction();
    auto status = txn->CreateInode(inode, mode, uid, gid);
    if (!status.OK()) {
        co_return status;
    }
    co_return txn->Commit();
}

AsyncTask<Status> MetaPartition::DeleteDentry(
    InodeID parent,
    const std::string& name
) {
    if (!store_) {
        co_return Status::IO("Store not initialized");
    }
    auto* rocksdb_store = dynamic_cast<RocksDBStore*>(store_.get());
    if (!rocksdb_store) {
        co_return Status::IO("Invalid store type");
    }
    co_return rocksdb_store->DeleteDentry(parent, name);
}

AsyncTask<Status> MetaPartition::DeleteInode(InodeID inode) {
    if (!store_) {
        co_return Status::IO("Store not initialized");
    }
    auto* rocksdb_store = dynamic_cast<RocksDBStore*>(store_.get());
    if (!rocksdb_store) {
        co_return Status::IO("Invalid store type");
    }
    co_return rocksdb_store->DeleteInode(inode);
}

AsyncTask<Status> MetaPartition::ListDentries(
    InodeID parent,
    std::vector<Dentry>* entries
) {
    if (!store_) {
        co_return Status::IO("Store not initialized");
    }
    auto* rocksdb_store = dynamic_cast<RocksDBStore*>(store_.get());
    if (!rocksdb_store) {
        co_return Status::IO("Invalid store type");
    }
    co_return rocksdb_store->ListDentries(parent, entries);
}

bool MetaPartition::ShouldSplit() const {
    return false;
}

std::pair<std::unique_ptr<MetaPartition>, std::unique_ptr<MetaPartition>>
MetaPartition::Split() {
    return {nullptr, nullptr};
}

} // namespace nebulastore::metadata
