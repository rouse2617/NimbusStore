#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include "nebulastore/common/types.h"

namespace nebulastore {

// SliceNode: 表示文件中的一个数据切片
struct SliceNode {
    uint64_t id;      // slice ID
    uint64_t size;    // slice 在存储中的总大小
    uint64_t off;     // slice 内部偏移
    uint64_t len;     // 使用的长度
    uint64_t pos;     // 在文件中的位置
    std::shared_ptr<SliceNode> left;
    std::shared_ptr<SliceNode> right;

    SliceNode(uint64_t pos_, uint64_t id_, uint64_t size_, uint64_t off_, uint64_t len_)
        : id(id_), size(size_), off(off_), len(len_), pos(pos_) {}

    uint64_t End() const { return pos + len; }
};

using SliceNodePtr = std::shared_ptr<SliceNode>;

// SliceTree: 管理文件的 slice 集合，处理重叠写入
class SliceTree {
public:
    SliceTree() = default;

    // 插入新 slice，处理重叠（Cut 算法）
    void Insert(uint64_t pos, uint64_t id, uint64_t size, uint64_t off, uint64_t len);

    // 查找包含指定位置的 slice
    SliceNodePtr Find(uint64_t pos) const;

    // 获取范围内的 slices
    std::vector<SliceNodePtr> GetRange(uint64_t start, uint64_t end) const;

    // 构建最终 SliceInfo 列表
    std::vector<SliceInfo> Build(const std::string& key_prefix) const;

    // 获取根节点
    SliceNodePtr Root() const { return root_; }

private:
    SliceNodePtr root_;

    // Cut 算法：切割被新 slice 覆盖的旧 slices
    SliceNodePtr Cut(SliceNodePtr node, uint64_t pos, uint64_t len);

    // BST 插入
    SliceNodePtr InsertNode(SliceNodePtr node, SliceNodePtr new_node);

    // 中序遍历收集 slices
    void InorderCollect(SliceNodePtr node, std::vector<SliceNodePtr>& result) const;

    // 范围查询辅助
    void RangeCollect(SliceNodePtr node, uint64_t start, uint64_t end,
                      std::vector<SliceNodePtr>& result) const;
};

} // namespace nebulastore
