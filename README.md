# NimbusStore

A high-performance, S3-compatible object storage system designed for AI training workloads.

## Features

- **S3 API Compatible** - Works with standard S3 clients (s3cmd, AWS CLI, boto3)
- **Pluggable Metadata Backend** - RocksDB (default), Redis, or custom backends
- **High Performance** - Optimized for AI/ML training data access patterns
- **Modern C++20** - Clean, maintainable codebase

## Quick Start

### Prerequisites

- GCC 11+ or Clang 14+ (C++20 support)
- RocksDB 9.x
- OpenSSL
- zlib

### Build

```bash
make
```

### Run

```bash
./build/nebula-master
```

The server starts on port 8080 by default.

### Test with s3cmd

```bash
# Configure s3cmd
cat > ~/.s3cfg << EOF
[default]
access_key = test
secret_key = test
host_base = localhost:8080
host_bucket = localhost:8080/%(bucket)
use_https = False
signature_v2 = True
EOF

# Create bucket
s3cmd mb s3://mybucket

# Upload file
s3cmd put myfile.txt s3://mybucket/myfile.txt

# Download file
s3cmd get s3://mybucket/myfile.txt downloaded.txt

# List objects
s3cmd ls s3://mybucket/
```

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    S3 API Layer                         │
│  (S3Handler, S3Router, S3XMLFormatter)                  │
├─────────────────────────────────────────────────────────┤
│                 Metadata Store                          │
│  (S3MetadataStore + Pluggable Backend)                  │
├─────────────────────────────────────────────────────────┤
│              Backend Interface                          │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐     │
│  │  RocksDB    │  │   Redis     │  │   Custom    │     │
│  │  Backend    │  │  Backend    │  │   Backend   │     │
│  └─────────────┘  └─────────────┘  └─────────────┘     │
├─────────────────────────────────────────────────────────┤
│                  Data Storage                           │
│  (Local Filesystem / S3 / Custom)                       │
└─────────────────────────────────────────────────────────┘
```

## Project Structure

```
NimbusStore/
├── include/nebulastore/
│   ├── protocol/          # S3 API implementation
│   │   ├── s3_handler.h   # S3 operation handlers
│   │   ├── s3_metadata.h  # Metadata store interface
│   │   ├── s3_router.h    # Request routing
│   │   └── s3_xml.h       # XML response generation
│   ├── metadata/          # Metadata service
│   ├── storage/           # Storage backends
│   └── common/            # Utilities
├── src/                   # Implementation files
├── tests/                 # Unit tests
└── third_party/           # Dependencies (mongoose)
```

## Supported S3 Operations

| Operation | Status |
|-----------|--------|
| ListBuckets | ✅ |
| CreateBucket | ✅ |
| DeleteBucket | ✅ |
| HeadBucket | ✅ |
| ListObjects | ✅ |
| ListObjectsV2 | ✅ |
| GetObject | ✅ |
| PutObject | ✅ |
| DeleteObject | ✅ |
| HeadObject | ✅ |

## Adding Custom Metadata Backend

Implement the `MetadataBackend` interface:

```cpp
class MyBackend : public MetadataBackend {
public:
    bool Put(const std::string& key, const std::string& value) override;
    bool Get(const std::string& key, std::string& value) override;
    bool Delete(const std::string& key) override;
    bool Exists(const std::string& key) override;
    bool BatchPut(const std::vector<std::pair<std::string, std::string>>& kvs) override;
    std::vector<std::pair<std::string, std::string>> Scan(
        const std::string& prefix, int limit = 1000) override;
};

// Register the backend
MetadataBackendFactory::Instance().Register("mybackend",
    [](const std::string& config) {
        return std::make_unique<MyBackend>(config);
    });
```

## Running Tests

```bash
make test       # Basic tests
make s3-test    # S3 module tests
```

## Roadmap / 未来规划

### Phase 1: Core Enhancement / 核心增强
- [ ] **Redis Metadata Backend** - Add Redis as alternative metadata storage / 添加 Redis 作为可选元数据存储
- [ ] **Authentication** - Implement AWS Signature V4 authentication / 实现 AWS Signature V4 认证
- [ ] **Multipart Upload** - Support large file uploads / 支持大文件分片上传
- [ ] **Object Versioning** - Enable version control for objects / 启用对象版本控制

### Phase 2: Distributed Architecture / 分布式架构
- [ ] **Raft Consensus** - Metadata replication with Raft / 使用 Raft 实现元数据复制
- [ ] **Data Sharding** - Horizontal scaling for data storage / 数据存储水平扩展
- [ ] **Load Balancing** - Distribute requests across nodes / 请求负载均衡

### Phase 3: AI Training Optimization / AI 训练优化
- [ ] **Batch Read API** - Optimized for training data loading / 针对训练数据加载优化的批量读取 API
- [ ] **Prefetching** - Intelligent data prefetch for sequential access / 顺序访问的智能数据预取
- [ ] **Caching Layer** - Multi-tier caching (memory/SSD/disk) / 多级缓存（内存/SSD/磁盘）
- [ ] **RDMA Support** - High-performance network for GPU clusters / GPU 集群高性能网络支持

### Phase 4: Enterprise Features / 企业级功能
- [ ] **POSIX Interface** - FUSE-based filesystem mount / 基于 FUSE 的文件系统挂载
- [ ] **Lifecycle Policies** - Automatic data tiering and expiration / 自动数据分层和过期策略
- [ ] **Cross-Region Replication** - Geo-distributed data redundancy / 跨地域数据冗余
- [ ] **Monitoring & Metrics** - Prometheus/Grafana integration / Prometheus/Grafana 集成

## Contributing / 贡献

Contributions are welcome! Please feel free to submit a Pull Request.

欢迎贡献代码！请随时提交 Pull Request。

## License

MIT License
