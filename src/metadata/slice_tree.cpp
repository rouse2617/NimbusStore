#include "nebulastore/metadata/slice_tree.h"
#include <algorithm>

namespace nebulastore {

// Cut 算法：处理新 slice 覆盖旧 slices 的情况
// 参考 JuiceFS 设计：新写入覆盖旧数据
SliceNodePtr SliceTree::Cut(SliceNodePtr node, uint64_t pos, uint64_t len) {
    if (!node) return nullptr;

    uint64_t end = pos + len;
    uint64_t node_end = node->End();

    // 递归处理左子树
    node->left = Cut(node->left, pos, len);
    // 递归处理右子树
    node->right = Cut(node->right, pos, len);

    // 检查当前节点是否与新 slice 重叠
    if (node_end <= pos || node->pos >= end) {
        // 无重叠，保留节点
        return node;
    }

    // 完全覆盖：删除节点
    if (node->pos >= pos && node_end <= end) {
        // 合并左右子树
        if (!node->left) return node->right;
        if (!node->right) return node->left;
        // 找右子树最小节点替换
        auto min_node = node->right;
        while (min_node->left) min_node = min_node->left;
        min_node->right = node->right;
        min_node->left = node->left;
        return min_node;
    }

    // 部分覆盖：需要切割
    if (node->pos < pos && node_end > end) {
        // 新 slice 在中间，分裂成两个节点
        auto right_part = std::make_shared<SliceNode>(
            end, node->id, node->size,
            node->off + (end - node->pos),
            node_end - end
        );
        node->len = pos - node->pos;
        node->right = InsertNode(node->right, right_part);
    } else if (node->pos < pos) {
        // 新 slice 覆盖右边部分
        node->len = pos - node->pos;
    } else {
        // 新 slice 覆盖左边部分
        uint64_t cut_len = end - node->pos;
        node->off += cut_len;
        node->len -= cut_len;
        node->pos = end;
    }

    return node;
}

// BST 插入
SliceNodePtr SliceTree::InsertNode(SliceNodePtr node, SliceNodePtr new_node) {
    if (!node) return new_node;

    if (new_node->pos < node->pos) {
        node->left = InsertNode(node->left, new_node);
    } else {
        node->right = InsertNode(node->right, new_node);
    }
    return node;
}

// 插入新 slice
void SliceTree::Insert(uint64_t pos, uint64_t id, uint64_t size, uint64_t off, uint64_t len) {
    // 先 Cut 掉被覆盖的部分
    root_ = Cut(root_, pos, len);
    // 插入新节点
    auto new_node = std::make_shared<SliceNode>(pos, id, size, off, len);
    root_ = InsertNode(root_, new_node);
}

// 中序遍历收集
void SliceTree::InorderCollect(SliceNodePtr node, std::vector<SliceNodePtr>& result) const {
    if (!node) return;
    InorderCollect(node->left, result);
    result.push_back(node);
    InorderCollect(node->right, result);
}

// 查找包含指定位置的 slice
SliceNodePtr SliceTree::Find(uint64_t pos) const {
    auto node = root_;
    while (node) {
        if (pos < node->pos) {
            node = node->left;
        } else if (pos >= node->End()) {
            node = node->right;
        } else {
            return node;
        }
    }
    return nullptr;
}

// 范围查询辅助
void SliceTree::RangeCollect(SliceNodePtr node, uint64_t start, uint64_t end,
                             std::vector<SliceNodePtr>& result) const {
    if (!node) return;

    if (node->pos >= end) {
        RangeCollect(node->left, start, end, result);
        return;
    }
    if (node->End() <= start) {
        RangeCollect(node->right, start, end, result);
        return;
    }

    RangeCollect(node->left, start, end, result);
    result.push_back(node);
    RangeCollect(node->right, start, end, result);
}

// 获取范围内的 slices
std::vector<SliceNodePtr> SliceTree::GetRange(uint64_t start, uint64_t end) const {
    std::vector<SliceNodePtr> result;
    RangeCollect(root_, start, end, result);
    return result;
}

// 构建最终 SliceInfo 列表
std::vector<SliceInfo> SliceTree::Build(const std::string& key_prefix) const {
    std::vector<SliceNodePtr> nodes;
    InorderCollect(root_, nodes);

    std::vector<SliceInfo> slices;
    slices.reserve(nodes.size());

    for (const auto& node : nodes) {
        SliceInfo info;
        info.slice_id = node->id;
        info.offset = node->pos;
        info.size = node->len;
        info.storage_key = key_prefix + "/" + std::to_string(node->id);
        slices.push_back(std::move(info));
    }

    return slices;
}

} // namespace nebulastore
