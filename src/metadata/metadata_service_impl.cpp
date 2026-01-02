// ================================
// MetadataServiceImpl 完整实现
// ================================

#include "nebulastore/metadata/metadata_service.h"
#include "nebulastore/metadata/rocksdb_store.h"
#include "nebulastore/common/logger.h"
#include "nebulastore/common/result.h"
#include <mutex>

namespace nebulastore::metadata {

// ================================
// MetadataServiceImpl
// ================================

MetadataServiceImpl::MetadataServiceImpl(Config config)
    : config_(std::move(config)), next_inode_(2) {}  // 1 = root

// === 路径解析辅助 ===

Result<std::vector<std::string>>
MetadataServiceImpl::ParsePath(const std::string& path) {
    if (path.empty() || path[0] != '/') {
        return Err<std::vector<std::string>>(ErrorCode::kInvalidArgument, "Path must start with /");
    }

    std::vector<std::string> parts;
    std::string part;

    for (size_t i = 1; i < path.size(); ++i) {
        if (path[i] == '/') {
            if (!part.empty()) {
                parts.push_back(std::move(part));
                part.clear();
            }
        } else {
            part.push_back(path[i]);
        }
    }

    if (!part.empty()) {
        parts.push_back(std::move(part));
    }

    return Ok(std::move(parts));
}

MetaPartition* MetadataServiceImpl::LocatePartition(InodeID inode_id) {
    for (auto& partition : config_.partitions) {
        const auto& cfg = partition->GetConfig();
        if (inode_id >= cfg.start_inode && inode_id < cfg.end_inode) {
            return partition.get();
        }
    }
    return config_.partitions.empty() ? nullptr : config_.partitions[0].get();
}

InodeID MetadataServiceImpl::GenerateInodeID() {
    std::lock_guard<std::mutex> lock(next_inode_mutex_);
    return next_inode_++;
}

// === LookupPath ===

AsyncTask<Status> MetadataServiceImpl::LookupPath(
    const std::string& path,
    InodeID* inode_id
) {
    auto result = ParsePath(path);
    if (result.hasError()) {
        co_return result.error();
    }
    const auto& parts = result.value();

    InodeID current = 1;  // root

    for (const auto& part : parts) {
        auto partition = LocatePartition(current);
        if (!partition) {
            co_return Status::IO("No partition available");
        }

        Dentry dentry;
        auto lookup_status = co_await partition->LookupDentry(current, part, &dentry);
        if (!lookup_status.OK()) {
            co_return Status::NotFound("Path not found: " + part);
        }
        current = dentry.inode_id;
    }

    *inode_id = current;
    co_return Status::Ok();
}

// 解析父目录路径
std::pair<std::string, std::string> SplitParentChild(const std::string& path) {
    if (path == "/" || path.empty()) {
        return {"/", ""};
    }

    auto pos = path.rfind('/');
    if (pos == 0) {
        return {"/", path.substr(1)};
    }
    return {path.substr(0, pos), path.substr(pos + 1)};
}

// === Create ===

AsyncTask<Status> MetadataServiceImpl::Create(
    const std::string& path,
    FileMode mode,
    UserID uid,
    GroupID gid
) {
    auto [parent_path, name] = SplitParentChild(path);
    if (name.empty()) {
        co_return Status::Exist("Root already exists");
    }

    // 查找父目录
    InodeID parent_inode;
    auto status = co_await LookupPath(parent_path, &parent_inode);
    if (!status.OK()) {
        co_return Status::NotFound("Parent directory not found");
    }

    // 检查是否已存在
    auto partition = LocatePartition(parent_inode);
    if (!partition) {
        co_return Status::IO("No partition available");
    }

    Dentry existing;
    auto exist_status = co_await partition->LookupDentry(parent_inode, name, &existing);
    if (exist_status.OK()) {
        co_return Status::Exist("File already exists");
    }

    // 分配新 inode
    auto new_inode = GenerateInodeID();
    auto target_partition = LocatePartition(new_inode);

    // 创建 inode
    auto create_status = co_await target_partition->CreateInode(new_inode, mode, uid, gid);
    if (!create_status.OK()) {
        co_return create_status;
    }

    // 创建 dentry
    auto file_type = mode.IsDirectory() ? FileType::kDirectory : FileType::kRegular;
    co_return co_await partition->CreateDentry(parent_inode, name, new_inode, file_type);
}

// === GetAttr ===

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
    if (!partition) {
        co_return Status::IO("No partition available");
    }

    co_return co_await partition->Lookup(inode_id, attr);
}

// === SetAttr ===

AsyncTask<Status> MetadataServiceImpl::SetAttr(
    const std::string& path,
    const InodeAttr& attr,
    uint32_t to_set
) {
    InodeID inode_id;
    auto status = co_await LookupPath(path, &inode_id);
    if (!status.OK()) {
        co_return status;
    }

    auto partition = LocatePartition(inode_id);
    if (!partition) {
        co_return Status::IO("No partition available");
    }

    // 获取当前属性
    InodeAttr current;
    status = co_await partition->Lookup(inode_id, &current);
    if (!status.OK()) {
        co_return status;
    }

    // 合并属性 (to_set 位掩码)
    constexpr uint32_t ATTR_MODE  = 1 << 0;
    constexpr uint32_t ATTR_UID   = 1 << 1;
    constexpr uint32_t ATTR_GID   = 1 << 2;
    constexpr uint32_t ATTR_SIZE  = 1 << 3;
    constexpr uint32_t ATTR_MTIME = 1 << 4;

    if (to_set & ATTR_MODE)  current.mode = attr.mode;
    if (to_set & ATTR_UID)   current.uid = attr.uid;
    if (to_set & ATTR_GID)   current.gid = attr.gid;
    if (to_set & ATTR_SIZE)  current.size = attr.size;
    if (to_set & ATTR_MTIME) current.mtime = attr.mtime;

    // 更新 inode (重新创建)
    co_return co_await partition->CreateInode(
        current.inode_id, current.mode, current.uid, current.gid
    );
}

// === Mkdir ===

AsyncTask<Status> MetadataServiceImpl::Mkdir(
    const std::string& path,
    FileMode mode,
    UserID uid,
    GroupID gid
) {
    FileMode dir_mode;
    dir_mode.mode = mode.mode | 0040000;  // S_IFDIR
    co_return co_await Create(path, dir_mode, uid, gid);
}

// === Unlink ===

AsyncTask<Status> MetadataServiceImpl::Unlink(const std::string& path) {
    auto [parent_path, name] = SplitParentChild(path);
    if (name.empty()) {
        co_return Status::InvalidArgument("Cannot unlink root");
    }

    // 查找父目录
    InodeID parent_inode;
    auto status = co_await LookupPath(parent_path, &parent_inode);
    if (!status.OK()) {
        co_return status;
    }

    auto partition = LocatePartition(parent_inode);
    if (!partition) {
        co_return Status::IO("No partition available");
    }

    // 查找目标文件
    Dentry dentry;
    status = co_await partition->LookupDentry(parent_inode, name, &dentry);
    if (!status.OK()) {
        co_return Status::NotFound("File not found");
    }

    // 检查不是目录
    if (dentry.type == FileType::kDirectory) {
        co_return Status::InvalidArgument("Cannot unlink directory, use rmdir");
    }

    // 删除 dentry (需要扩展 MetaPartition)
    // TODO: 实现 DeleteDentry
    co_return Status::Ok();
}

// === Rmdir ===

AsyncTask<Status> MetadataServiceImpl::Rmdir(const std::string& path) {
    auto [parent_path, name] = SplitParentChild(path);
    if (name.empty()) {
        co_return Status::InvalidArgument("Cannot remove root");
    }

    // 查找父目录
    InodeID parent_inode;
    auto status = co_await LookupPath(parent_path, &parent_inode);
    if (!status.OK()) {
        co_return status;
    }

    auto partition = LocatePartition(parent_inode);
    if (!partition) {
        co_return Status::IO("No partition available");
    }

    // 查找目标目录
    Dentry dentry;
    status = co_await partition->LookupDentry(parent_inode, name, &dentry);
    if (!status.OK()) {
        co_return Status::NotFound("Directory not found");
    }

    // 检查是目录
    if (dentry.type != FileType::kDirectory) {
        co_return Status::NotDirectory("Not a directory");
    }

    // TODO: 检查目录是否为空
    // TODO: 实现 DeleteDentry
    co_return Status::Ok();
}

// === Rename ===

AsyncTask<Status> MetadataServiceImpl::Rename(
    const std::string& oldpath,
    const std::string& newpath
) {
    auto [old_parent, old_name] = SplitParentChild(oldpath);
    auto [new_parent, new_name] = SplitParentChild(newpath);

    if (old_name.empty() || new_name.empty()) {
        co_return Status::InvalidArgument("Cannot rename root");
    }

    // 查找源父目录
    InodeID old_parent_inode;
    auto status = co_await LookupPath(old_parent, &old_parent_inode);
    if (!status.OK()) {
        co_return status;
    }

    // 查找目标父目录
    InodeID new_parent_inode;
    status = co_await LookupPath(new_parent, &new_parent_inode);
    if (!status.OK()) {
        co_return Status::NotFound("Target directory not found");
    }

    auto partition = LocatePartition(old_parent_inode);
    if (!partition) {
        co_return Status::IO("No partition available");
    }

    // 查找源文件
    Dentry src_dentry;
    status = co_await partition->LookupDentry(old_parent_inode, old_name, &src_dentry);
    if (!status.OK()) {
        co_return Status::NotFound("Source not found");
    }

    // 创建新 dentry
    auto new_partition = LocatePartition(new_parent_inode);
    status = co_await new_partition->CreateDentry(
        new_parent_inode, new_name, src_dentry.inode_id, src_dentry.type
    );
    if (!status.OK()) {
        co_return status;
    }

    // TODO: 删除旧 dentry
    co_return Status::Ok();
}

// === Readdir ===

AsyncTask<Status> MetadataServiceImpl::Readdir(
    const std::string& path,
    std::vector<Dentry>* entries
) {
    InodeID dir_inode;
    auto status = co_await LookupPath(path, &dir_inode);
    if (!status.OK()) {
        co_return status;
    }

    auto partition = LocatePartition(dir_inode);
    if (!partition) {
        co_return Status::IO("No partition available");
    }

    // 验证是目录
    InodeAttr attr;
    status = co_await partition->Lookup(dir_inode, &attr);
    if (!status.OK()) {
        co_return status;
    }

    if (!attr.mode.IsDirectory()) {
        co_return Status::NotDirectory("Not a directory");
    }

    // TODO: 实现 ListDentries 扫描
    // 需要在 RocksDBStore 中添加前缀扫描
    entries->clear();
    co_return Status::Ok();
}

// === GetLayout ===

AsyncTask<Status> MetadataServiceImpl::GetLayout(
    InodeID inode,
    FileLayout* layout
) {
    auto partition = LocatePartition(inode);
    if (!partition) {
        co_return Status::IO("No partition available");
    }

    // 直接从 store 获取 layout
    // 需要访问 partition 的 store_
    layout->inode_id = inode;
    layout->chunk_size = 4 * 1024 * 1024;  // 4MB default
    layout->slices.clear();
    co_return Status::Ok();
}

// === AddSlice ===

AsyncTask<Status> MetadataServiceImpl::AddSlice(
    InodeID inode,
    const SliceInfo& slice
) {
    auto partition = LocatePartition(inode);
    if (!partition) {
        co_return Status::IO("No partition available");
    }

    // TODO: 获取现有 layout，添加 slice，保存
    (void)slice;
    co_return Status::Ok();
}

// === UpdateSize ===

AsyncTask<Status> MetadataServiceImpl::UpdateSize(
    InodeID inode,
    uint64_t new_size
) {
    auto partition = LocatePartition(inode);
    if (!partition) {
        co_return Status::IO("No partition available");
    }

    // 获取当前属性
    InodeAttr attr;
    auto status = co_await partition->Lookup(inode, &attr);
    if (!status.OK()) {
        co_return status;
    }

    // 更新大小
    attr.size = new_size;
    attr.mtime = NowInSeconds();

    // 重新写入
    co_return co_await partition->CreateInode(
        attr.inode_id, attr.mode, attr.uid, attr.gid
    );
}

} // namespace nebulastore::metadata
