// ================================
// 元数据分区实现骨架 - 等你来完善
// ================================

#include "nebulastore/metadata/metadata_service.h"
#include "nebulastore/metadata/rocksdb_store.h"
#include "nebulastore/metadata/btree_index.h"
#include "nebulastore/common/logger.h"

namespace yig::metadata {

// ================================
// MetaPartition
// ================================

MetaPartition::MetaPartition(const Config& config)
    : config_(config),
      mode_(ScaleMode::kStandalone),
      inode_tree_(nullptr),
      dentry_tree_(nullptr) {}

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
    if (auto cached = inode_tree_->GetInode(inode_id, attr)) {
        co_return Status::OK();
    }

    // 2. 内存未命中，查 RocksDB
    auto status = store_->LookupInode(inode_id, attr);
    if (!status.OK()) {
        co_return status;
    }

    // 3. 更新内存缓存
    inode_tree_->InsertInode(inode_id, *attr);

    co_return Status::OK();
}

AsyncTask<Status> MetaPartition::CreateDentry(
    InodeID parent,
    const std::string& name,
    InodeID inode,
    FileType type
) {
    // 1. 查找父目录
    InodeAttr parent_attr;
    auto status = co_await Lookup(parent, &parent_attr);
    if (!status.OK()) {
        co_return Status::NotFound("Parent directory not found");
    }

    // 2. 创建 dentry (使用事务)
    auto txn = store_->BeginTransaction();
    status = txn->CreateDentry(parent, name, inode, type);
    if (!status.OK()) {
        co_return status;
    }
    status = txn->Commit();
    if (!status.OK()) {
        co_return status;
    }

    // 3. 更新内存 BTree
    dentry_tree_->InsertDentry(parent, name, {name, inode, type});

    co_return Status::OK();
}

AsyncTask<Status> MetaPartition::CreateInode(
    InodeID inode,
    FileMode mode,
    UserID uid,
    GroupID gid
) {
    // 1. 创建 inode (使用事务)
    auto txn = store_->BeginTransaction();
    auto status = txn->CreateInode(inode, mode, uid, gid);
    if (!status.OK()) {
        co_return status;
    }
    status = txn->Commit();
    if (!status.OK()) {
        co_return status;
    }

    // 2. 更新内存 BTree
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
    inode_tree_->InsertInode(inode, attr);

    co_return Status::OK();
}

bool MetaPartition::ShouldSplit() const {
    // 沧海规模自适应: 10 亿对象触发分裂
    if (mode_ == ScaleMode::kStandalone) {
        return inode_tree_->Size() > 1e9;
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
    : config_(std::move(config)) {}

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

    // 2. 查找父目录 inode
    InodeID parent_inode = 0;  // root inode
    for (size_t i = 0; i < parts.size() - 1; ++i) {
        InodeAttr attr;
        status = co_await LookupPath("/" + parts[i], &parent_inode);
        if (!status.OK()) {
            co_return status;
        }
    }

    // 3. 分配新 inode
    auto new_inode = GenerateInodeID();

    // 4. 创建 inode
    auto partition = LocatePartition(new_inode);
    status = co_await partition->CreateInode(new_inode, mode, uid, gid);
    if (!status.OK()) {
        co_return status;
    }

    // 5. 创建 dentry
    auto name = parts.empty() ? "" : parts.back();
    status = co_await partition->CreateDentry(
        parent_inode,
        name,
        new_inode,
        mode.IsDirectory() ? FileType::kDirectory : FileType::kRegular
    );

    co_return status;
}

std::pair<Status, std::vector<std::string>>
MetadataServiceImpl::ParsePath(const std::string& path) {
    // TODO: 实现 /a/b/c → ["a", "b", "c"]
    return {Status::OK(), {"a", "b", "c"}};
}

MetaPartition* MetadataServiceImpl::LocatePartition(InodeID inode_id) {
    // TODO: 根据 inode_id 找到对应的分区
    // 简化版: 返回第一个分区
    return config_.partitions[0].get();
}

InodeID MetadataServiceImpl::GenerateInodeID() {
    // TODO: 使用分布式 ID 生成器
    static InodeID next_id = 1;
    return next_id++;
}

} // namespace yig::metadata
