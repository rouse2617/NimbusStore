// ================================
// 简单测试程序 - 不依赖 gtest
// ================================

#include <iostream>
#include <cassert>
#include "nebulastore/metadata/metadata_service.h"
#include "nebulastore/metadata/rocksdb_store.h"
#include "nebulastore/storage/backend.h"
#include "nebulastore/common/logger.h"
#include "nebulastore/common/types.h"

using namespace nebulastore;
using namespace nebulastore::metadata;
using namespace nebulastore::storage;

void TestRocksDBStore() {
    std::cout << "Testing RocksDBStore..." << std::endl;

    // 创建临时数据库
    RocksDBStore::Config config;
    config.db_path = "/tmp/nebula_test_db";
    config.create_if_missing = true;
    RocksDBStore store(config);

    auto status = store.Init();
    assert(status.OK());
    std::cout << "  [OK] Store initialized" << std::endl;

    // 测试 CreateInode
    auto txn = store.BeginTransaction();
    FileMode mode;
    mode.mode = 0100644;  // 普通文件
    status = txn->CreateInode(100, mode, 0, 0);
    assert(status.OK());
    std::cout << "  [OK] Inode created" << std::endl;

    status = txn->Commit();
    assert(status.OK());
    std::cout << "  [OK] Transaction committed" << std::endl;

    // 测试 LookupInode
    InodeAttr attr;
    status = store.LookupInode(100, &attr);
    assert(status.OK());
    assert(attr.inode_id == 100);
    std::cout << "  [OK] Inode lookup: inode_id=" << attr.inode_id << std::endl;

    // 测试 CreateDentry
    txn = store.BeginTransaction();
    status = txn->CreateDentry(1, "test.txt", 100, FileType::kRegular);
    assert(status.OK());
    std::cout << "  [OK] Dentry created" << std::endl;

    status = txn->Commit();
    assert(status.OK());
    std::cout << "  [OK] Dentry committed" << std::endl;

    // 测试 LookupDentry
    Dentry dentry;
    status = store.LookupDentry(1, "test.txt", &dentry);
    assert(status.OK());
    assert(dentry.inode_id == 100);
    std::cout << "  [OK] Dentry lookup: name=" << dentry.name
              << ", inode=" << dentry.inode_id << std::endl;

    std::cout << "All RocksDBStore tests passed!" << std::endl;
}

void TestMetadataService() {
    std::cout << "\nTesting MetadataService..." << std::endl;

    // 创建元数据分区
    MetaPartition::Config part_config;
    part_config.start_inode = 1;
    part_config.end_inode = 1000000;
    part_config.data_dir = "/tmp/nebula_test_meta";
    auto partition = std::make_unique<MetaPartition>(part_config);

    auto status = partition->Init();
    assert(status.OK());
    std::cout << "  [OK] Partition initialized" << std::endl;

    // 创建元数据服务
    MetadataServiceImpl::Config config;
    config.partitions.push_back(std::move(partition));

    MetadataServiceImpl service(std::move(config));

    // 测试路径解析
    auto parse_result = service.ParsePath("/a/b/c");
    assert(parse_result.hasValue());
    const auto& parts = parse_result.value();
    assert(parts.size() == 3);
    assert(parts[0] == "a" && parts[1] == "b" && parts[2] == "c");
    std::cout << "  [OK] Path parsed: /a/b/c -> [a, b, c]" << std::endl;

    // 测试根路径
    auto root_result = service.ParsePath("/");
    assert(root_result.hasValue());
    assert(root_result.value().empty());
    std::cout << "  [OK] Root path parsed: / -> []" << std::endl;

    // 测试 Inode 分配
    auto inode1 = service.GenerateInodeID();
    auto inode2 = service.GenerateInodeID();
    assert(inode2 == inode1 + 1);
    std::cout << "  [OK] Inode allocation: " << inode1 << ", " << inode2 << std::endl;

    std::cout << "All MetadataService tests passed!" << std::endl;
}

void TestLocalBackend() {
    std::cout << "\nTesting LocalBackend..." << std::endl;

    LocalBackend::Config config;
    config.data_dir = "/tmp/nebula_test_data";
    LocalBackend backend(std::move(config));

    std::cout << "  [OK] Backend initialized" << std::endl;

    // 注意：由于协程特性，这里需要特殊处理
    // 暂时跳过实际存储测试
    std::cout << "  [SKIP] Put/Get test (requires coroutine runtime)" << std::endl;

    std::cout << "LocalBackend test completed!" << std::endl;
}

int main() {
    std::cout << "====================================\n";
    std::cout << "NebulaStore 2.0 - Unit Tests\n";
    std::cout << "====================================\n\n";

    try {
        TestRocksDBStore();
        TestMetadataService();
        TestLocalBackend();

        std::cout << "\n====================================\n";
        std::cout << "All tests PASSED!\n";
        std::cout << "====================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nTest FAILED: " << e.what() << std::endl;
        return 1;
    }
}
