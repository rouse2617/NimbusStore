# NebulaStore 2.0

> 基于 JuiceFS、CubeFS、DeepSeek 3FS、百度沧海存储融合设计的 AI 训练存储系统

## 架构设计

详细的架构设计文档请查看: [docs/NEBULA2.0_ARCHITECTURE.md](../docs/NEBULA2.0_ARCHITECTURE.md)

## 技术栈

| 组件 | 技术 |
|-----|------|
| **语言** | C++20 |
| **元数据存储** | RocksDB + Raft (可演进到 FoundationDB) |
| **数据存储** | 可插拔 (S3/MinIO/Ceph/Local) |
| **网络** | HTTP (S3) + FUSE (POSIX) + RDMA (可选) |
| **构建** | CMake 3.20+ |

## 目录结构

```
nebulastore/
├── include/nebulastore/   # 公共头文件
│   ├── common/            # 通用组件
│   │   ├── types.h        # 基础类型定义
│   │   ├── async.h        # 协程支持
│   │   └── logger.h       # 日志系统
│   ├── metadata/          # 元数据服务
│   │   ├── metadata_service.h  # 元数据服务接口
│   │   ├── rocksdb_store.h     # RocksDB 存储
│   │   └── btree_index.h       # 内存 BTree 索引
│   ├── storage/           # 存储后端
│   │   └── backend.h      # 存储后端接口
│   ├── namespace/         # 统一命名空间
│   │   └── service.h      # 路径转换 + 统一查询
│   └── protocol/          # 协议网关
│       └── gateway.h      # S3 + POSIX 网关
├── src/                   # 实现文件
├── tests/                 # 测试
├── cmake/                 # CMake 模块
└── scripts/               # 构建和部署脚本
```

## 快速开始

### 依赖安装

```bash
# Ubuntu/Debian
sudo apt update
sudo apt install -y \
    cmake \
    g++ \
    libspdlog-dev \
    librocksdb-dev \
    libbz2-dev \
    liblz4-dev \
    libsnappy-dev \
    libfuse3-dev \
    libssl-dev

# 可选: RDMA 支持
sudo apt install -y \
    libibverbs-dev \
    librdmacm-dev
```

### 构建

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 运行

```bash
# 启动元数据服务
./nebula-master --config ../configs/master.yaml

# 启动 S3 Gateway
./nebula-s3-gateway --config ../configs/s3_gateway.yaml

# 挂载 POSIX 文件系统
./nebula-fuse --config ../configs/fuse.yaml /mnt/nebula
```

## 使用示例

### S3 API

```bash
# 上传文件
aws s3 cp /path/to/file.txt s3://bucket/data/file.txt --endpoint-url http://localhost:8080

# 下载文件
aws s3 cp s3://bucket/data/file.txt /tmp/file.txt --endpoint-url http://localhost:8080

# 列出文件
aws s3 ls s3://bucket/data/ --endpoint-url http://localhost:8080
```

### POSIX API

```bash
# 同一份数据，可以直接访问
ls /mnt/nebula/data/
cat /mnt/nebula/data/file.txt
```

## 待实现

框架已搭建完成，以下是需要完善的核心模块：

- [ ] **RocksDBStore** - 元数据持久化 (`src/metadata/rocksdb_store.cpp`)
- [ ] **MetaPartition** - 元数据分区管理 (`src/metadata/metadata_partition.cpp`)
- [ ] **MetadataServiceImpl** - 元数据服务实现 (`src/metadata/metadata_service_impl.cpp`)
- [ ] **PathConverter** - 路径转换 (`src/namespace/service.cpp`)
- [ ] **S3Backend** - S3 存储后端 (`src/storage/s3_backend.cpp`)
- [ ] **LocalBackend** - 本地存储后端 (`src/storage/local_backend.cpp`)
- [ ] **S3Gateway** - S3 协议实现 (`src/protocol/s3_gateway.cpp`)
- [ ] **FuseClient** - POSIX 实现 (`src/protocol/fuse_client.cpp`)

## 开发指南

### 元数据操作流程

```cpp
// 1. 创建文件
auto service = NewMetadataService(config);
auto status = co_await service->Create("/data/test.txt", 0644, 0, 0);

// 2. 写入数据
auto storage = NewStorageBackend(config);
co_await storage->Put("chunks/1/0", data);

// 3. 添加 slice
co_await service->AddSlice(1, {0, 0, data.size(), "chunks/1/0"});
```

### 路径转换

```cpp
// S3 → POSIX
auto converter = PathConverter("bucket");
auto posix = converter.S3ToPosix("s3://bucket/data/file.txt");  // /data/file.txt

// POSIX → S3
auto s3 = converter.PosixToS3("/data/file.txt");  // s3://bucket/data/file.txt
```

## 许可证

MIT License
