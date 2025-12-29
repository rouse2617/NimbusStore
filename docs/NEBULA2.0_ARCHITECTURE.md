# NebulaStore 2.0 架构设计文档

> 基于 Ceph RGW 分析的存算分离 AI 训练存储系统

## 目录

1. [设计原则](#设计原则)
2. [架构概览](#架构概览)
3. [核心模块设计](#核心模块设计)
4. [Ceph RGW 借鉴模式](#ceph-rgw-借鉴模式)
5. [数据模型](#数据模型)
6. [接口设计](#接口设计)
7. [开发路线图](#开发路线图)

---

## 设计原则

### 1. 存算分离 (JuiceFS)
- **元数据**: RocksDB 持久化 + 内存 BTree 索引
- **数据**: 可插拔存储后端 (S3/Local/Ceph)

### 2. 分层抽象 (Ceph RGW SAL)
```
┌─────────────────────────────────────┐
│   Protocol Layer (S3/FUSE/gRPC)    │
├─────────────────────────────────────┤
│   Store Abstraction Layer (SAL)    │
├─────────────────────────────────────┤
│   Backend Implementations          │
└─────────────────────────────────────┘
```

### 3. 无状态设计 (DeepSeek 3FS)
- MetadataService 无状态，可水平扩展
- 状态存储在 MetaPartition (通过 Raft 一致性)

### 4. 规模自适应 (百度沧海)
- 小规模: 单机模式 (< 10 亿对象)
- 大规模: 分布式模式 (≥ 10 亿对象)

---

## 架构概览

```
┌────────────────────────────────────────────────────────────────┐
│                         Protocol Layer                        │
├────────────────┬─────────────────┬────────────────────────────┤
│  S3 Gateway    │  FUSE Client    │  gRPC API (未来)           │
│  (HTTP/REST)   │  (POSIX)        │                            │
└────────┬───────┴────────┬────────┴────────────┬───────────────┘
         │                │                     │
┌────────▼────────────────▼─────────────────────▼───────────────┐
│                    Namespace Service                          │
│  (S3 ↔ POSIX 路径转换 + 统一命名空间)                         │
└────────┬──────────────────────────────────────────────────────┘
         │
┌────────▼──────────────────────────────────────────────────────┐
│              Metadata Service (SAL - 顶层)                    │
├───────────────────────────────────────────────────────────────┤
│  - LookupPath  - Create  - GetAttr  - Readdir                │
│  - AddSlice    - UpdateSize  - GetLayout                     │
└────────┬──────────────────────────────────────────────────────┘
         │
┌────────▼──────────────────────────────────────────────────────┐
│              MetaPartition Manager (SAL - 中层)               │
├───────────────────────────────────────────────────────────────┤
│  Partition 1     Partition 2     Partition 3    ...           │
│  [1-10M)         [10M-20M)       [20M-30M)                   │
└────────┬──────────────────────────────────────────────────────┘
         │
┌────────▼──────────────────────────────────────────────────────┐
│         MetadataStore (SAL - 底层) + StorageBackend          │
├──────────────────────┬───────────────────────────────────────┤
│  RocksDB Store       │  S3 Backend / Local Backend           │
│  (元数据持久化)      │  (数据存储)                           │
└──────────────────────┴───────────────────────────────────────┘
```

---

## 核心模块设计

### 1. Store Abstraction Layer (SAL)

基于 Ceph RGW 的 SAL 设计，提供三层抽象：

```cpp
namespace nebulastore::sal {

// === 层次 1: Store (顶层入口) ===
class Store {
public:
    virtual ~Store() = default;
    virtual const char* get_name() const = 0;

    // 获取 User
    virtual std::unique_ptr<User> get_user(const std::string& user_id) = 0;

    // 获取 Bucket (S3 概念)
    virtual std::unique_ptr<Bucket> get_bucket(const std::string& bucket) = 0;
};

// === 层次 2: User / Bucket ===
class User {
public:
    virtual int list_buckets(const ListParams& params, BucketList& result) = 0;
    virtual int create_bucket(const std::string& name, Bucket** bucket) = 0;
    virtual int load_user() = 0;
    virtual int store_user() = 0;
};

class Bucket {
public:
    struct ListParams {
        std::string prefix;
        std::string delim;
        std::string marker;
        uint64_t max = 1000;
    };

    struct ListResults {
        std::vector<ObjectInfo> objects;
        std::vector<std::string> common_prefixes;
        bool is_truncated = false;
        std::string next_marker;
    };

    virtual int list(const ListParams& params, ListResults& results) = 0;
    virtual int get_object(const std::string& key, Object** obj) = 0;
};

// === 层次 3: Object ===
class Object {
public:
    virtual int read(uint64_t offset, uint64_t size, bufferlist& bl) = 0;
    virtual int write(uint64_t offset, bufferlist& bl) = 0;
    virtual int get_attr(Attrs& attrs) = 0;
    virtual int set_attr(const Attrs& attrs) = 0;
};

} // namespace nebulastore::sal
```

### 2. 异步 I/O 抽象

借鉴 Ceph RGW 的 AIO 设计：

```cpp
namespace nebulastore::io {

// 异步操作结果
struct AioResult {
    uint64_t id;              // 请求 ID
    bufferlist data;          // 读取数据
    int result = 0;           // 返回码
    void* user_data = nullptr;  // 用户数据

    // 用于链表
    boost::intrusive::list_base_hook<> hook;
};

// Aio 接口
class Aio {
public:
    using OpFunc = std::function<void(Aio*, AioResult&)>;

    virtual ~Aio() = default;

    // 提交异步操作
    virtual void submit(const std::string& oid,
                        OpFunc&& op,
                        uint64_t id) = 0;

    // 等待完成
    virtual int drain() = 0;

    // 获取已完成的操作
    virtual AioResultList get_completed() = 0;
};

// Aio 节流器 (并发控制)
class AioThrottle {
public:
    AioThrottle(uint64_t max_concurrent = 1000,
                uint64_t max_pending = 10000);

    // 提交操作（自动节流）
    int submit(Aio* aio, const std::string& oid,
               Aio::OpFunc&& op, uint64_t id);

    // 等待所有完成
    int drain();

private:
    uint64_t max_concurrent_;
    uint64_t max_pending_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace nebulastore::io
```

### 3. 元数据分区 (MetaPartition)

```cpp
namespace nebulastore::metadata {

class MetaPartition {
public:
    struct Config {
        uint64_t start_inode;    // 起始 Inode ID
        uint64_t end_inode;      // 结束 Inode ID
        std::string db_path;     // RocksDB 路径
    };

    // 规模模式
    enum class ScaleMode {
        kStandalone,   // 单机: < 10 亿对象
        kDistributed   // 分布式: ≥ 10 亿对象
    };

    // === 核心操作 ===

    // 创建文件 (原子操作: dentry + inode)
    AsyncTask<Status> Create(
        InodeID parent,
        const std::string& name,
        FileMode mode,
        UserID uid,
        GroupID gid
    );

    // 查找路径
    AsyncTask<Status> LookupPath(
        const std::string& path,
        InodeID* inode_id
    );

    // 添加 Slice (JuiceFS 风格)
    AsyncTask<Status> AddSlice(
        InodeID inode,
        const SliceInfo& slice
    );

    // === 规模自适应 ===

    bool ShouldSplit() const;
    std::pair<MetaPartition*, MetaPartition*> Split();

private:
    // 内存索引 (热数据缓存)
    std::unique_ptr<BTreeIndex> inode_index_;
    std::unique_ptr<BTreeIndex> dentry_index_;

    // 持久化存储
    std::unique_ptr<RocksDBStore> store_;

    // Raft 状态机 (分布式模式)
    std::unique_ptr<RaftStateMachine> raft_;
};

} // namespace nebulastore::metadata
```

### 4. 存储后端 (StorageBackend)

```cpp
namespace nebulastore::storage {

class StorageBackend {
public:
    virtual ~StorageBackend() = default;

    // === 基础 CRUD ===
    virtual AsyncTask<Status> Put(
        const std::string& key,
        const ByteBuffer& data
    ) = 0;

    virtual AsyncTask<Status> Get(
        const std::string& key,
        ByteBuffer* data
    ) = 0;

    virtual AsyncTask<Status> Delete(
        const std::string& key
    ) = 0;

    // === AI 训练优化 ===

    // 批量读取 (用于数据加载)
    virtual AsyncTask<Status> BatchGet(
        const std::vector<std::string>& keys,
        std::vector<ByteBuffer>* data
    ) = 0;

    // 范围读取 (用于随机访问)
    virtual AsyncTask<Status> GetRange(
        const std::string& key,
        uint64_t offset,
        uint64_t size,
        ByteBuffer* data
    ) = 0;
};

// S3 后端
class S3Backend : public StorageBackend {
public:
    struct Config {
        std::string access_key;
        std::string secret_key;
        std::string region;
        std::string endpoint;
        std::string bucket;
    };

    explicit S3Backend(Config config);
    // ... 实现
};

// 本地后端 (开发测试)
class LocalBackend : public StorageBackend {
public:
    struct Config {
        std::string data_dir;
    };

    explicit LocalBackend(Config config);
    // ... 实现
};

} // namespace nebulastore::storage
```

---

## Ceph RGW 借鉴模式

### 1. 操作模式 (RGWOp)

```cpp
namespace nebulastore::op {

// 基础操作类
class Operation {
public:
    struct ReqState {
        std::string method;      // GET/PUT/DELETE
        std::string path;
        std::map<std::string, std::string> params;
        std::map<std::string, std::string> headers;
    };

    virtual ~Operation() = default;

    // 执行操作
    virtual int execute(const ReqState& state) = 0;

    // 权限检查
    virtual int verify_permission() = 0;

    // 发送响应
    virtual void send_response() = 0;
};

// 具体操作示例
class PutObjectOp : public Operation {
public:
    int execute(const ReqState& state) override;
    int verify_permission() override;
    void send_response() override;

private:
    std::string bucket_;
    std::string key_;
    bufferlist data_;
};

class GetObjectOp : public Operation {
    // ...
};

} // namespace nebulastore::op
```

### 2. 数据处理器 (ObjectProcessor)

```cpp
namespace nebulastore::processor {

// 流式数据处理 (用于大文件上传)
class DataProcessor {
public:
    virtual ~DataProcessor() = default;

    // 处理数据块
    virtual int process(bufferlist&& data, uint64_t offset) = 0;

    // 完成
    virtual int complete(
        size_t accounted_size,
        const std::string& etag,
        const std::map<std::string, std::string>& attrs
    ) = 0;
};

// Object 写入处理器
class ObjectWriter : public DataProcessor {
public:
    ObjectWriter(StorageBackend* backend,
                 const std::string& key);

    int process(bufferlist&& data, uint64_t offset) override;
    int complete(size_t size, const std::string& etag,
                 const std::map<std::string, std::string>& attrs) override;

private:
    StorageBackend* backend_;
    std::string key_;
    bufferlist buffer_;
};

} // namespace nebulastore::processor
```

### 3. ACL 系统

```cpp
namespace nebulastore::acl {

// 访问控制列表
class ACL {
public:
    enum class Permission {
        READ,
        WRITE,
        READ_ACP,
        WRITE_ACP,
        FULL_CONTROL
    };

    struct Grant {
        std::string grantee;  // 用户 ID
        std::set<Permission> permissions;
    };

    void add_grant(const Grant& grant);
    bool check_permission(const std::string& user_id,
                          Permission perm) const;

    // 序列化
    std::string to_xml() const;
    static ACL from_xml(const std::string& xml);

private:
    std::vector<Grant> grants_;
};

} // namespace nebulastore::acl
```

---

## 数据模型

### RocksDB Key-Value 设计

```
┌─────────────────────────────────────────────────────────────┐
│                       Key Schema                           │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Dentry:    "D" + parent_inode_id + "/" + name             │
│  Inode:     "I" + inode_id                                 │
│  Layout:    "L" + inode_id                                 │
│  User:      "U" + user_id                                  │
│  Bucket:    "B" + bucket_name                              │
│                                                             │
├─────────────────────────────────────────────────────────────┤
│                       Value Schema                         │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Dentry:    inode_id (8 bytes) + type (1 byte)             │
│  Inode:     mode + uid + gid + size + mtime + nlink        │
│  Layout:    slice_count + [slice_offset, size, key]*       │
│  User:      display_name + access_keys + acls              │
│  Bucket:    owner + creation_time + acls + layout          │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### FileLayout (JuiceFS Slice 设计)

```cpp
struct SliceInfo {
    uint64_t offset;      // 文件偏移
    uint64_t size;        // 切片大小
    std::string chunk_key;  // 存储后端 key
};

struct FileLayout {
    uint64_t file_size;
    std::vector<SliceInfo> slices;

    // 查找包含某个偏移的 slice
    const SliceInfo* FindSlice(uint64_t offset) const {
        auto it = std::upper_bound(
            slices.begin(), slices.end(), offset,
            [](uint64_t off, const SliceInfo& s) {
                return off < s.offset + s.size;
            }
        );
        return (it != slices.end()) ? &*it : nullptr;
    }
};
```

---

## 接口设计

### S3 Gateway 接口

```cpp
namespace nebulastore::gateway {

class S3Gateway {
public:
    struct Config {
        std::string listen_addr = "0.0.0.0";
        uint16_t port = 8080;
        std::shared_ptr<sal::Store> store;
    };

    explicit S3Gateway(Config config);

    // 启动服务
    int start();

    // HTTP 处理
    void handle_request(const http::Request& req,
                       http::Response& resp);

private:
    // 操作路由
    std::unique_ptr<op::Operation> create_op(
        const std::string& method,
        const std::string& path
    );

    Config config_;
    std::unique_ptr<http::Server> server_;
};

} // namespace nebulastore::gateway
```

### FUSE 接口

```cpp
namespace nebulastore::fuse {

class FuseClient {
public:
    struct Config {
        std::string mount_point;
        std::shared_ptr<sal::Store> store;
    };

    explicit FuseClient(Config config);

    // 挂载
    int mount();

    // FUSE 操作实现
    int getattr(const char* path, struct stat* stbuf);
    int readdir(const char* path, void* buf,
                fuse_fill_dir_t filler);
    int open(const char* path, struct fuse_file_info* fi);
    int read(const char* path, char* buf, size_t size,
             off_t offset, struct fuse_file_info* fi);
    int write(const char* path, const char* buf,
              size_t size, off_t offset,
              struct fuse_file_info* fi);
    int create(const char* path, mode_t mode,
               struct fuse_file_info* fi);

private:
    Config config_;
    std::shared_ptr<sal::Store> store_;
};

} // namespace nebulastore::fuse
```

---

## 开发路线图

### 阶段 0: 核心元数据 (1-2 周)

**目标**: 实现最基础的文件系统功能

```
[优先级 1] 元数据存储层
├── RocksDBStore 实现
│   ├── LookupDentry
│   ├── LookupInode
│   ├── CreateDentry
│   └── CreateInode
└── RocksDBCodec (Key/Value 编解码)

[优先级 2] 元数据服务层
├── MetadataServiceImpl
│   ├── Create (文件/目录)
│   ├── Lookup (路径解析)
│   ├── GetAttr
│   └── Readdir
└── MetaPartition
    ├── 路径解析
    └── Inode 分配

[优先级 3] 本地存储后端
├── LocalBackend 实现
│   ├── Put
│   ├── Get
│   └── Delete
└── 单元测试
```

**验收标准**:
```bash
./tests/stage0_test
# ✓ CreateFile
# ✓ WriteData
# ✓ ReadData
# ✓ ListDirectory
# ✓ DeleteFile
```

---

### 阶段 1: FUSE 支持 (2-3 周)

**目标**: 实现 POSIX 文件系统接口

```
[优先级 1] FUSE 客户端
├── FuseClient 基础框架
│   ├── mount/unmount
│   ├── getattr
│   ├── readdir
│   ├── open/release
│   └── read/write
└── 文件句柄管理

[优先级 2] 数据读写
├── 文件布局管理
│   ├── GetLayout
│   ├── AddSlice
│   └── FindSlice
└── Slice 管理
    ├── 分配 Slice ID
    └── Slice 元数据持久化

[优先级 3] 缓存层
├── 文件数据缓存
└── 元数据缓存
```

**验收标准**:
```bash
# 挂载文件系统
./nebula-fuse /mnt/nebula /tmp/nebula.db

# 运行 POSIX 测试
cd /mnt/nebula
echo "Hello" > test.txt
cat test.txt  # 输出: Hello
ls -la        # 显示文件列表
```

---

### 阶段 2: S3 Gateway (2-3 周)

**目标**: 实现 S3 协议支持

```
[优先级 1] HTTP 服务器
├── HTTP/HTTPS 监听
├── 请求解析
└── 响应生成

[优先级 2] S3 协议实现
├── 签名验证 (AWS V4)
│   ├── SignatureCalc
│   └── CanonicalRequest
├── Bucket 操作
│   ├── CreateBucket
│   ├── DeleteBucket
│   ├── ListBuckets
│   └── HeadBucket
└── Object 操作
    ├── PutObject
    ├── GetObject
    ├── DeleteObject
    ├── ListObjects
    └── HeadObject

[优先级 3] ACL 系统
├── ACL 存储
├── ACL 验证
└── S3 ACL 策略解析
```

**验收标准**:
```bash
# 启动 S3 Gateway
./nebula-s3-gateway --config configs/s3.yaml

# 使用 AWS CLI 测试
aws s3 mb s3://test-bucket --endpoint-url http://localhost:8080
aws s3 cp file.txt s3://test-bucket/ --endpoint-url http://localhost:8080
aws s3 ls s3://test-bucket/ --endpoint-url http://localhost:8080
```

---

### 阶段 3: S3 存储后端 (1-2 周)

**目标**: 支持 S3 作为数据存储

```
[优先级 1] S3 Client
├── AWS SDK 集成
├── 连接池管理
└── 重试逻辑

[优先级 2] S3Backend 实现
├── Put (分片上传)
├── Get (范围读取)
├── Delete
└── BatchGet (批量读取)
```

---

### 阶段 4: 高级特性 (按需)

```
[ ] 多部分上传 (Multipart Upload)
[ ] 生命周期管理
[ ] 对象加密 (SSE-KMS/SSE-C)
[ ] 版本控制
[ ] 跨域资源共享 (CORS)
[ ] 事件通知
[ ] 对象锁定
[ ] 对象标签
[ ] 缓存层优化
[ ] RDMA 支持
```

---

## 从哪里开始？

### 推荐起点: **阶段 0 - 元数据核心**

**理由**:
1. 元数据是整个系统的基础
2. 可以独立测试，不依赖外部服务
3. 验证核心架构设计

### 第一个文件: `src/metadata/rocksdb_store.cpp`

```cpp
#include "nebulastore/metadata/rocksdb_store.h"
#include "nebulastore/common/logger.h"

namespace nebulastore::metadata {

Status RocksDBStore::Init(const std::string& db_path) {
    rocksdb::Options options;
    options.create_if_missing = true;

    rocksdb::Status status =
        rocksdb::DB::Open(options, db_path, &db_);
    if (!status.ok()) {
        return Status::Internal("Failed to open RocksDB: " +
                                status.ToString());
    }
    return Status::OK();
}

Status RocksDBStore::LookupDentry(
    InodeID parent,
    const std::string& name,
    Dentry* dentry
) {
    auto key = codec_.EncodeDentryKey(parent, name);
    std::string value;
    auto status = db_->Get(rocksdb::ReadOptions(), key, &value);

    if (status.IsNotFound()) {
        return Status::NotFound("Dentry not found");
    }
    if (!status.ok()) {
        return Status::IO(status.ToString());
    }

    *dentry = codec_.DecodeDentryValue(value);
    return Status::OK();
}

// ... 继续实现其他方法

} // namespace nebulastore::metadata
```

### 编译验证

```bash
cd /home/hrp/yig/yig-2.0
mkdir build && cd build
cmake ..
make nebula-metadata
```

### 运行测试

```bash
./tests/stage0_test
```

---

## 下一步行动

1. **立即开始**: 实现 `RocksDBStore`
2. **验证路径**: 运行 `stage0_test`
3. **完成阶段 0**: 实现基本的 CRUD 操作

需要我帮你实现具体的代码吗？
