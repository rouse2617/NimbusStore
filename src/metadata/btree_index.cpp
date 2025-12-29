// ================================
// BTree 索引实现骨架 - 等你来完善
// ================================

#include "nebulastore/metadata/btree_index.h"

namespace nebulastore::metadata {

// ================================
// BTreeIndex
// ================================

BTreeIndex::BTreeIndex() {}
BTreeIndex::~BTreeIndex() = default;

bool BTreeIndex::InsertInode(InodeID inode_id, const InodeAttr& attr) {
    return inode_index_.Insert(inode_id, attr);
}

bool BTreeIndex::GetInode(InodeID inode_id, InodeAttr* attr) {
    return inode_index_.Get(inode_id, attr);
}

bool BTreeIndex::DeleteInode(InodeID inode_id) {
    return inode_index_.Delete(inode_id);
}

bool BTreeIndex::InsertDentry(
    InodeID parent,
    const std::string& name,
    const Dentry& dentry
) {
    auto key = std::make_pair(parent, name);
    return dentry_index_.Insert(key, dentry);
}

bool BTreeIndex::GetDentry(
    InodeID parent,
    const std::string& name,
    Dentry* dentry
) {
    auto key = std::make_pair(parent, name);
    return dentry_index_.Get(key, dentry);
}

bool BTreeIndex::DeleteDentry(InodeID parent, const std::string& name) {
    auto key = std::make_pair(parent, name);
    return dentry_index_.Delete(key);
}

} // namespace nebulastore::metadata
