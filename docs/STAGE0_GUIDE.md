# 阶段 0 实现指南

## 目标

实现最基本的功能，验证架构可行性：

```cpp
// 创建文件
Create("/data/test.txt", 0644);

// 写入数据
Write("/data/test.txt", "Hello, World!");

// 读取数据
auto data = Read("/data/test.txt");  // "Hello, World!"

// 列出目录
auto files = ListDirectory("/data");  // ["test.txt"]
```

## 实现顺序

### Step 1: 完善 RocksDB 查询 (1-2 天)

**文件**: `src/metadata/rocksdb_store.cpp`

```cpp
Status RocksDBStore::LookupDentry(
    InodeID parent,
    const std::string& name,
    Dentry* dentry
) {
    auto key = EncodeDentryKey(parent, name);
    std::string value;
    auto status = db_->Get(rocksdb::ReadOptions(), key, &value);

    if (status.IsNotFound()) {
        return Status::NotFound("Dentry not found");
    }
    if (!status.ok()) {
        return Status::IO(status.ToString());
    }

    *dentry = DecodeDentryValue(value);
    return Status::OK();
}

Status RocksDBStore::LookupInode(
    InodeID inode,
    InodeAttr* attr
) {
    auto key = EncodeInodeKey(inode);
    std::string value;
    auto status = db_->Get(rocksdb::ReadOptions(), key, &value);

    if (status.IsNotFound()) {
        return Status::NotFound("Inode not found");
    }
    if (!status.ok()) {
        return Status::IO(status.ToString());
    }

    *attr = DecodeInodeValue(value);
    return Status::OK();
}
```

**验证**: 编译通过

---

### Step 2: 实现路径解析 (1 天)

**文件**: `src/metadata/metadata_partition.cpp`

```cpp
std::pair<Status, std::vector<std::string>>
MetadataServiceImpl::ParsePath(const std::string& path) {
    // "/a/b/c" → ["a", "b", "c"]
    // "/" → []

    if (path.empty() || path[0] != '/') {
        return {Status::InvalidArgument("Invalid path"), {}};
    }

    std::vector<std::string> parts;
    std::string part;

    for (size_t i = 1; i < path.size(); ++i) {
        if (path[i] == '/') {
            if (!part.empty()) {
                parts.push_back(part);
                part.clear();
            }
        } else {
            part.push_back(path[i]);
        }
    }

    if (!part.empty()) {
        parts.push_back(part);
    }

    return {Status::OK(), parts};
}
```

**验证**: 运行单元测试

---

### Step 3: 实现根目录创建 (1 天)

```cpp
AsyncTask<Status> MetadataServiceImpl::CreateRoot() {
    // 创建根目录 inode (inode_id = 1)
    InodeID root_inode = 1;
    auto partition = LocatePartition(root_inode);

    FileMode mode;
    mode.mode = 0040555 | 0040000;  // 目录权限

    co_return co_await partition->CreateInode(
        root_inode,
        mode,
        0,  // uid
        0   // gid
    );
}
```

---

### Step 4: 实现文件创建 (2-3 天)

这是最核心的功能，需要：
1. 分配 inode
2. 创建 dentry (父目录 → 文件名)
3. 创建 inode

**验证**: 测试用例通过

---

## 验收标准

```bash
# 运行测试
cd build && ./tests/stage0_test

# 预期输出:
# [==========] Running 5 tests from 1 test suite.
# [----------] Stage0Test (5 tests)
# [ RUN      ] Stage0Test.CreateDirectory
# [       OK ] Stage0Test.CreateDirectory
# [ RUN      ] Stage0Test.CreateFile
# [       OK ] Stage0Test.CreateFile
# [ RUN      ] Stage0Test.WriteData
# [       OK ] Stage0Test.WriteData
# [ RUN      ] Stage0Test.ListDirectory
# [       OK ] Stage0Test.ListDirectory
# [ RUN      ] Stage0Test.DeleteFile
# [       OK ] Stage0Test.DeleteFile
# [==========] 5 tests from 1 test suite ran. (XX ms total)
```

## 下一步

完成阶段 0 后，我们就有了：
- ✅ 可工作的元数据系统
- ✅ 可工作的存储后端
- ✅ 完整的测试

然后可以进入**阶段 1**，添加 FUSE 支持，实现真正的文件系统。

## 需要帮助？

遇到问题随时问我，我会继续帮你完善。
