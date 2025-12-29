// ================================
// 元数据分区实现
// 基于 CubeFS + DeepSeek 3FS 设计
// ================================

#include "nebulastore/metadata/metadata_service.h"
#include "nebulastore/metadata/rocksdb_store.h"
#include "nebulastore/metadata/btree_index.h"
#include "nebulastore/common/logger.h"
#include <mutex>

namespace nebulastore::metadata {

// ================================
// MetaPartition
// ================================

MetaPartition::MetaPartition(const Config& config)
    : config_(config),
      mode_(ScaleMode::kStandalone),
      inode_tree_(std::make_unique<BTreeIndex>()),
      dentry_tree_(std::make_unique<BTreeIndex>()) {}

MetaPartition::~MetaPartition() = default;

Status MetaPartition::Init() {
    // 初始化 RocksDB 存储
    auto store = std::make_unique<RocksDBStore>(
        RocksDBStore::Config{config_.data_dir}
    );
    auto status = store->Init();
    if (!status.OK()) {
        return status;
    }

    store_ = std::move(store);

    // 初始化内存 BTree 索引
    // TODO: 从 RocksDB 加载热数据到内存

    LOG_INFO("MetaPartition initialized: range [{}, {})",
             config_.start_inode, config_.end_inode);
    return Status::OK();
}

AsyncTask<Status> MetaPartition::Lookup(
    InodeID inode_id,
    InodeAttr* attr
) {
    // 1. 先查内存 BTree (CubeFS 设计)
    if (inode_tree_ && inode_tree_->GetInode(inode_id, attr)) {
        co_return Status::OK();
    }

    // 2. 内存未命中，查 RocksDB
    if (!store_) {
        co_return Status::IO("Store not initialized");
    }

    auto status = store_->LookupInode(inode_id, attr);
    if (!status.OK()) {
        co_return status;
    }

    // 3. 更新内存缓存
    if (inode_tree_) {
        inode_tree_->InsertInode(inode_id, *attr);
    }

    co_return Status::OK();
}

AsyncTask<Status> MetaPartition::LookupDentry(
    InodeID parent,
    const std::string& name,
    Dentry* dentry
) {
    // 1. 先查内存 BTree
    if (dentry_tree_) {
        Dentry cached;
        if (dentry_tree_->GetDentry(parent, name, &cached)) {
            *dentry = cached;
            co_return Status::OK();
        }
    }

    // 2. 内存未命中，查 RocksDB
    if (!store_) {
        co_return Status::IO("Store not initialized");
    }

    auto status = store_->LookupDentry(parent, name, dentry);
    if (!status.OK()) {
        co_return status;
    }

    // 3. 更新内存缓存
    if (dentry_tree_) {
        dentry_tree_->InsertDentry(parent, name, *dentry);
    }

    co_return Status::OK();
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

    // 1. 检查父目录是否存在
    InodeAttr parent_attr;
    auto status = co_await Lookup(parent, &parent_attr);
    if (!status.OK()) {
        co_return Status::NotFound("Parent directory not found");
    }

    // 确保父目录是目录类型
    if (!parent_attr.mode.IsDirectory()) {
        co_return Status::NotDirectory("Parent is not a directory");
    }

    // 2. 检查是否已存在
    Dentry existing;
    status = co_await LookupDentry(parent, name, &existing);
    if (status.OK()) {
        co_return Status::Exist("File already exists");
    }

    // 3. 创建 dentry (使用事务)
    auto txn = store_->BeginTransaction();
    status = txn->CreateDentry(parent, name, inode, type);
    if (!status.OK()) {
        co_return status;
    }
    status = txn->Commit();
    if (!status.OK()) {
        co_return status;
    }

    // 4. 更新内存 BTree
    if (dentry_tree_) {
        dentry_tree_->InsertDentry(parent, name, {name, inode, type});
    }

    co_return Status::OK();
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

    // 1. 检查 inode 是否在范围内
    if (inode < config_.start_inode || inode >= config_.end_inode) {
        co_return Status::InvalidArgument("Inode ID out of range");
    }

    // 2. 检查是否已存在
    InodeAttr existing;
    auto status = co_await Lookup(inode, &existing);
    if (status.OK()) {
        co_return Status::Exist("Inode already exists");
    }

    // 3. 创建 inode (使用事务)
    auto txn = store_->BeginTransaction();
    status = txn->CreateInode(inode, mode, uid, gid);
    if (!status.OK()) {
        co_return status;
    }
    status = txn->Commit();
    if (!status.OK()) {
        co_return status;
    }

    // 4. 更新内存 BTree
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
    if (inode_tree_) {
        inode_tree_->InsertInode(inode, attr);
    }

    co_return Status::OK();
}

bool MetaPartition::ShouldSplit() const {
    // 沧海规模自适应: 10 亿对象触发分裂
    if (mode_ == ScaleMode::kStandalone) {
        return inode_tree_ && inode_tree_->Size() > 1e9;
    }
    return false;
}

std::pair<std::unique_ptr<MetaPartition>, std::unique_ptr<MetaPartition>>
MetaPartition::Split() {
    // 切换到分布式模式
    mode_ = ScaleMode::kDistributed;

    auto mid = (config_.start_inode + config_.end_inode) / 2;

    Config left_config{config_.start_inode, mid, config_.data_dir + "_left"};
    Config right_config{mid, config_.end_inode, config_.data_dir + "_right"};

    auto left = std::make_unique<MetaPartition>(left_config);
    auto right = std::make_unique<MetaPartition>(right_config);

    // TODO: 分裂数据到两个新分区

    LOG_INFO("Partition split: [{}, {}) → [{}, {}) + [{}, {})",
             config_.start_inode, config_.end_inode,
             left_config.start_inode, left_config.end_inode,
             right_config.start_inode, right_config.end_inode);

    return {std::move(left), std::move(right)};
}

// ================================
// MetadataServiceImpl
// ================================

MetadataServiceImpl::MetadataServiceImpl(Config config)
    : config_(std::move(config)), next_inode_(2) {}  // 1 = root

AsyncTask<Status> MetadataServiceImpl::Create(
    const std::string& path,
    FileMode mode,
    UserID uid,
    GroupID gid
) {
    // 1. 解析路径
    auto [status, parts] = ParsePath(path);
    if (!status.OK()) {
        co_return status;
    }

    if (parts.empty()) {
        // 根目录已存在
        co_return Status::Exist("Root already exists");
    }

    // 2. 查找父目录
    InodeID parent_inode = 1;  // root inode = 1
    std::string parent_path = "/";

    for (size_t i = 0; i < parts.size() - 1; ++i) {
        Dentry dentry;
        auto part_status = co_await LookupPath(parent_path + parts[i], &parent_inode);
        if (!part_status.OK()) {
            co_return Status::NotFound("Parent directory not found: " + parts[i]);
        }
    }

    // 3. 分配新 inode
    auto new_inode = GenerateInodeID();

    // 4. 创建 inode
    auto partition = LocatePartition(new_inode);
    auto create_status = co_await partition->CreateInode(new_inode, mode, uid, gid);
    if (!create_status.OK()) {
        co_return create_status;
    }

    // 5. 创建 dentry
    auto name = parts.back();
    auto dentry_status = co_await partition->CreateDentry(
        parent_inode,
        name,
        new_inode,
        mode.IsDirectory() ? FileType::kDirectory : FileType::kRegular
    );

    co_return dentry_status;
}

AsyncTask<Status> MetadataServiceImpl::GetAttr(
    const std::string& path,
    InodeAttr* attr
) {
    InodeID inode_id;
    auto status = co_await LookupPath(path, &inode_id);
    if (!status.OK()) {
        co_return status;
    }

    auto partition = LocatePartition(inode_id);
    co_return co_await partition->Lookup(inode_id, attr);
}

AsyncTask<Status> MetadataServiceImpl::Mkdir(
    const std::string& path,
    FileMode mode,
    UserID uid,
    GroupID gid
) {
    // 设置目录权限
    FileMode dir_mode;
    dir_mode.mode = mode.mode | 0040000;  // S_IFDIR

    co_return co_await Create(path, dir_mode, uid, gid);
}

AsyncTask<Status> MetadataServiceImpl::LookupPath(
    const std::string& path,
    InodeID* inode_id
) {
    auto [status, parts] = ParsePath(path);
    if (!status.OK()) {
        co_return status;
    }

    InodeID current = 1;  // root inode

    if (parts.empty()) {
        *inode_id = current;
        co_return Status::OK();
    }

    for (const auto& part : parts) {
        auto partition = LocatePartition(current);
        Dentry dentry;
        auto lookup_status = co_await partition->LookupDentry(current, part, &dentry);
        if (!lookup_status.OK()) {
            co_return Status::NotFound("Path component not found: " + part);
        }
        current = dentry.inode_id;
    }

    *inode_id = current;
    co_return Status::OK();
}

std::pair<Status, std::vector<std::string>>
MetadataServiceImpl::ParsePath(const std::string& path) {
    // /a/b/c → ["a", "b", "c"]
    // / → []

    if (path.empty() || path[0] != '/') {
        return {Status::InvalidArgument("Invalid path"), {}};
    }

    std::vector<std::string> parts;
    std::string part;

    for (size_t i = 1; i < path.size(); ++i) {
        if (path[i] == '/') {
            if (!part.empty()) {
                parts.push_back(part);
                part.clear();
            }
        } else {
            part.push_back(path[i]);
        }
    }

    if (!part.empty()) {
        parts.push_back(part);
    }

    return {Status::OK(), parts};
}

MetaPartition* MetadataServiceImpl::LocatePartition(InodeID inode_id) {
    // 根据 inode_id 找到对应的分区
    for (auto& partition : config_.partitions) {
        if (inode_id >= partition->config_.start_inode &&
            inode_id < partition->config_.end_inode) {
            return partition.get();
        }
    }
    return config_.partitions.empty() ? nullptr : config_.partitions[0].get();
}

InodeID MetadataServiceImpl::GenerateInodeID() {
    std::lock_guard<std::mutex> lock(next_inode_mutex_);
    return next_inode_++;
}

// Stub implementations for remaining methods

AsyncTask<Status> MetadataServiceImpl::SetAttr(
    const std::string& path,
    const InodeAttr& attr,
    uint32_t to_set
) {
    co_return Status::OK();
}

AsyncTask<Status> MetadataServiceImpl::Unlink(
    const std::string& path
) {
    co_return Status::OK();
}

AsyncTask<Status> MetadataServiceImpl::Rmdir(
    const std::string& path
) {
    co_return Status::OK();
}

AsyncTask<Status> MetadataServiceImpl::Rename(
    const std::string& oldpath,
    const std::string& newpath
) {
    co_return Status::OK();
}

AsyncTask<Status> MetadataServiceImpl::Readdir(
    const std::string& path,
    std::vector<Dentry>* entries
) {
    co_return Status::OK();
}

AsyncTask<Status> MetadataServiceImpl::GetLayout(
    InodeID inode,
    FileLayout* layout
) {
    co_return Status::OK();
}

AsyncTask<Status> MetadataServiceImpl::AddSlice(
    InodeID inode,
    const SliceInfo& slice
) {
    co_return Status::OK();
}

AsyncTask<Status> MetadataServiceImpl::UpdateSize(
    InodeID inode,
    uint64_t new_size
) {
    co_return Status::OK();
}

} // namespace nebulastore::metadata
