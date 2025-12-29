#pragma once

#include <memory>
#include <map>
#include <vector>

namespace aifs::metadata {

// ================================
// 内存 BTree 索引 (CubeFS 设计)
// ================================

// 简单的 BTree 实现 (可替换为 abseil::btree_map)
template<typename K, typename V>
class BTree {
public:
    using Iterator = typename std::map<K, V>::iterator;
    using ConstIterator = typename std::map<K, V>::const_iterator;

    bool Insert(const K& key, const V& value) {
        auto [it, inserted] = map_.emplace(key, value);
        return inserted;
    }

    bool Get(const K& key, V* value) const {
        auto it = map_.find(key);
        if (it != map_.end()) {
            *value = it->second;
            return true;
        }
        return false;
    }

    V* Get(const K& key) {
        auto it = map_.find(key);
        return it != map_.end() ? &it->second : nullptr;
    }

    bool Delete(const K& key) {
        return map_.erase(key) > 0;
    }

    size_t Size() const {
        return map_.size();
    }

    Iterator Begin() { return map_.begin(); }
    Iterator End() { return map_.end(); }
    ConstIterator Begin() const { return map_.begin(); }
    ConstIterator End() const { return map_.end(); }

private:
    std::map<K, V> map_;
};

// ================================
// 内存索引管理
// ================================

class BTreeIndex {
public:
    BTreeIndex();
    ~BTreeIndex();

    // === inode 索引 ===

    bool InsertInode(InodeID inode_id, const InodeAttr& attr);
    bool GetInode(InodeID inode_id, InodeAttr* attr);
    bool DeleteInode(InodeID inode_id);

    // === dentry 索引 ===

    // key: parent_inode + name
    using DentryKey = std::pair<InodeID, std::string>;

    bool InsertDentry(InodeID parent, const std::string& name, const Dentry& dentry);
    bool GetDentry(InodeID parent, const std::string& name, Dentry* dentry);
    bool DeleteDentry(InodeID parent, const std::string& name);

    // === 统计 ===

    size_t InodeCount() const { return inode_index_.Size(); }
    size_t DentryCount() const { return dentry_index_.Size(); }

private:
    BTree<InodeID, InodeAttr> inode_index_;
    BTree<DentryKey, Dentry> dentry_index_;
};

} // namespace aifs::metadata
