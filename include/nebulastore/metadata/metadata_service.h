#pragma once

#include <memory>
#include <vector>
#include <string>
#include "nebulastore/common/types.h"
#include "nebulastore/common/async.h"

namespace nebulastore::metadata {

// ================================
// 元数据服务接口 (3FS 无状态设计)
// ================================

class MetadataService {
public:
    virtual ~MetadataService() = default;

    // === 文件/目录操作 ===

    // 创建文件或目录
    virtual AsyncTask<Status> Create(
        const std::string& path,
        FileMode mode,
        UserID uid,
        GroupID gid
    ) = 0;

    // 查询文件属性
    virtual AsyncTask<Status> GetAttr(
        const std::string& path,
        InodeAttr* attr
    ) = 0;

    // 设置文件属性
    virtual AsyncTask<Status> SetAttr(
        const std::string& path,
        const InodeAttr& attr,
        uint32_t to_set
    ) = 0;

    // 删除文件
    virtual AsyncTask<Status> Unlink(
        const std::string& path
    ) = 0;

    // 删除目录
    virtual AsyncTask<Status> Rmdir(
        const std::string& path
    ) = 0;

    // 创建目录
    virtual AsyncTask<Status> Mkdir(
        const std::string& path,
        FileMode mode,
        UserID uid,
        GroupID gid
    ) = 0;

    // 重命名
    virtual AsyncTask<Status> Rename(
        const std::string& oldpath,
        const std::string& newpath
    ) = 0;

    // 列出目录
    virtual AsyncTask<Status> Readdir(
        const std::string& path,
        std::vector<Dentry>* entries
    ) = 0;

    // === 文件布局管理 (JuiceFS 风格) ===

    // 获取文件布局
    virtual AsyncTask<Status> GetLayout(
        InodeID inode,
        FileLayout* layout
    ) = 0;

    // 添加 slice
    virtual AsyncTask<Status> AddSlice(
        InodeID inode,
        const SliceInfo& slice
    ) = 0;

    // 更新文件大小
    virtual AsyncTask<Status> UpdateSize(
        InodeID inode,
        uint64_t new_size
    ) = 0;

    // === 查找操作 ===

    // 路径解析: /a/b/c → inode_id
    virtual AsyncTask<Status> LookupPath(
        const std::string& path,
        InodeID* inode_id
    ) = 0;
};

// ================================
// 元数据存储接口 (RocksDB 抽象)
// ================================

class MetadataStore {
public:
    virtual ~MetadataStore() = default;

    // === 事务支持 ===
    class Transaction {
    public:
        virtual ~Transaction() = default;

        // 创建 dentry
        virtual Status CreateDentry(
            InodeID parent,
            const std::string& name,
            InodeID inode,
            FileType type
        ) = 0;

        // 创建 inode
        virtual Status CreateInode(
            InodeID inode,
            FileMode mode,
            UserID uid,
            GroupID gid
        ) = 0;

        // 提交事务
        virtual Status Commit() = 0;

        // 回滚事务
        virtual Status Rollback() = 0;
    };

    // 开启事务
    virtual std::unique_ptr<Transaction> BeginTransaction() = 0;

    // === 快照查询 (无需事务) ===

    // 查询 dentry
    virtual Status LookupDentry(
        InodeID parent,
        const std::string& name,
        Dentry* dentry
    ) = 0;

    // 查询 inode
    virtual Status LookupInode(
        InodeID inode,
        InodeAttr* attr
    ) = 0;

    // 查询文件布局
    virtual Status LookupLayout(
        InodeID inode,
        FileLayout* layout
    ) = 0;
};

// ================================
// 元数据分区 (CubeFS Range 分片 + 沧海规模自适应)
// ================================

class MetaPartition {
public:
    struct Config {
        uint64_t start_inode;
        uint64_t end_inode;
        std::string data_dir;  // RocksDB 数据目录
    };

    explicit MetaPartition(const Config& config);
    ~MetaPartition();

    // 初始化
    Status Init();

    // === 查询操作 ===

    // 查找 inode (内存 BTree)
    AsyncTask<Status> Lookup(
        InodeID inode_id,
        InodeAttr* attr
    );

    // 查找 dentry (内存 BTree)
    AsyncTask<Status> LookupDentry(
        InodeID parent,
        const std::string& name,
        Dentry* dentry
    );

    // === 修改操作 ===

    // 创建 dentry
    AsyncTask<Status> CreateDentry(
        InodeID parent,
        const std::string& name,
        InodeID inode,
        FileType type
    );

    // 创建 inode
    AsyncTask<Status> CreateInode(
        InodeID inode,
        FileMode mode,
        UserID uid,
        GroupID gid
    );

    // === 规模自适应 (沧海设计) ===

    enum class ScaleMode {
        kStandalone,   // 单机模式：< 10 亿对象
        kDistributed   // 分布式模式：≥ 10 亿对象
    };

    ScaleMode GetScaleMode() const { return mode_; }

    // 检查是否需要分裂
    bool ShouldSplit() const;

    // 分裂为两个分区
    std::pair<std::unique_ptr<MetaPartition>, std::unique_ptr<MetaPartition>> Split();

private:
    Config config_;
    ScaleMode mode_;

    // 内存索引 (CubeFS 设计)
    class BTreeIndex;
    std::unique_ptr<BTreeIndex> inode_tree_;
    std::unique_ptr<BTreeIndex> dentry_tree_;

    // RocksDB 存储
    std::unique_ptr<MetadataStore> store_;
};

// ================================
// 元数据服务实现 (无状态代理)
// ================================

class MetadataServiceImpl : public MetadataService {
public:
    struct Config {
        // 元数据分区列表
        std::vector<std::unique_ptr<MetaPartition>> partitions;

        // Raft 配置 (用于分布式)
        struct RaftConfig {
            uint16_t node_id;
            std::vector<std::string> peers;
        };
        std::optional<RaftConfig> raft_config;
    };

    explicit MetadataServiceImpl(Config config);
    ~MetadataServiceImpl() override = default;

    // === 实现 MetadataService 接口 ===

    AsyncTask<Status> Create(
        const std::string& path,
        FileMode mode,
        UserID uid,
        GroupID gid
    ) override;

    AsyncTask<Status> GetAttr(
        const std::string& path,
        InodeAttr* attr
    ) override;

    AsyncTask<Status> SetAttr(
        const std::string& path,
        const InodeAttr& attr,
        uint32_t to_set
    ) override;

    AsyncTask<Status> Unlink(
        const std::string& path
    ) override;

    AsyncTask<Status> Rmdir(
        const std::string& path
    ) override;

    AsyncTask<Status> Mkdir(
        const std::string& path,
        FileMode mode,
        UserID uid,
        GroupID gid
    ) override;

    AsyncTask<Status> Rename(
        const std::string& oldpath,
        const std::string& newpath
    ) override;

    AsyncTask<Status> Readdir(
        const std::string& path,
        std::vector<Dentry>* entries
    ) override;

    AsyncTask<Status> GetLayout(
        InodeID inode,
        FileLayout* layout
    ) override;

    AsyncTask<Status> AddSlice(
        InodeID inode,
        const SliceInfo& slice
    ) override;

    AsyncTask<Status> UpdateSize(
        InodeID inode,
        uint64_t new_size
    ) override;

    AsyncTask<Status> LookupPath(
        const std::string& path,
        InodeID* inode_id
    ) override;

private:
    // 路径解析: /a/b/c → ["a", "b", "c"]
    std::pair<Status, std::vector<std::string>> ParsePath(
        const std::string& path
    );

    // 根据 inode_id 查找对应的分区
    MetaPartition* LocatePartition(InodeID inode_id);

    // 生成新的 inode ID
    InodeID GenerateInodeID();

    Config config_;
    std::mutex next_inode_mutex_;
    InodeID next_inode_ = 2;  // 1 = root
};

} // namespace nebulastore::metadata
