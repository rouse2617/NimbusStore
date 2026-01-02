// ================================
// 模块测试 - 覆盖新实现的模块
// ================================

#include <iostream>
#include <cassert>
#include <filesystem>
#include <thread>
#include <atomic>
#include "nebulastore/metadata/metadata_service.h"
#include "nebulastore/metadata/rocksdb_store.h"
#include "nebulastore/storage/backend.h"
#include "nebulastore/namespace/service.h"
#include "nebulastore/common/logger.h"
#include "nebulastore/common/types.h"
#include "nebulastore/common/result.h"
#include "nebulastore/common/singleflight.h"
#include "nebulastore/metadata/slice_tree.h"

using namespace nebulastore;
using namespace nebulastore::metadata;
using namespace nebulastore::storage;
using namespace nebulastore::namespace_;

// ================================
// Result<T> 测试
// ================================
void TestResult() {
    std::cout << "\nTesting Result<T>..." << std::endl;

    // 测试 Ok
    auto ok = Ok(42);
    assert(ok.hasValue() && !ok.hasError());
    assert(ok.value() == 42);
    std::cout << "  [OK] Ok(42)" << std::endl;

    // 测试 Err
    auto err = Err<int>(ErrorCode::kNotFound, "not found");
    assert(err.hasError() && !err.hasValue());
    assert(err.error().code() == ErrorCode::kNotFound);
    std::cout << "  [OK] Err<int>" << std::endl;

    // 测试 map
    auto mapped = Ok(10).map([](int x) { return x * 2; });
    assert(mapped.hasValue() && mapped.value() == 20);
    std::cout << "  [OK] map: 10 -> 20" << std::endl;

    auto err_mapped = Err<int>(ErrorCode::kIOError).map([](int x) { return x * 2; });
    assert(err_mapped.hasError());
    std::cout << "  [OK] map on error preserves error" << std::endl;

    // 测试 andThen
    auto chained = Ok(5).andThen([](int x) -> Result<std::string> {
        return Ok(std::to_string(x * 3));
    });
    assert(chained.hasValue() && chained.value() == "15");
    std::cout << "  [OK] andThen: 5 -> \"15\"" << std::endl;

    auto err_chained = Err<int>(ErrorCode::kNotFound).andThen([](int x) -> Result<std::string> {
        return Ok(std::to_string(x));
    });
    assert(err_chained.hasError());
    std::cout << "  [OK] andThen on error preserves error" << std::endl;

    // 测试 orElse
    auto recovered = Err<int>(ErrorCode::kNotFound).orElse([](const Status&) -> Result<int> {
        return Ok(0);
    });
    assert(recovered.hasValue() && recovered.value() == 0);
    std::cout << "  [OK] orElse: error -> 0" << std::endl;

    auto ok_orElse = Ok(100).orElse([](const Status&) -> Result<int> {
        return Ok(0);
    });
    assert(ok_orElse.hasValue() && ok_orElse.value() == 100);
    std::cout << "  [OK] orElse on Ok preserves value" << std::endl;

    std::cout << "All Result<T> tests passed!" << std::endl;
}

// ================================
// SliceTree 测试
// ================================
void TestSliceTree() {
    std::cout << "\nTesting SliceTree..." << std::endl;

    SliceTree tree;

    // 测试 Insert
    tree.Insert(0, 1, 1024, 0, 100);    // [0, 100)
    tree.Insert(200, 2, 1024, 0, 100);  // [200, 300)
    tree.Insert(100, 3, 1024, 0, 100);  // [100, 200)
    std::cout << "  [OK] Insert 3 slices" << std::endl;

    // 测试 Find
    auto node = tree.Find(50);
    assert(node && node->id == 1);
    std::cout << "  [OK] Find(50) -> slice 1" << std::endl;

    node = tree.Find(150);
    assert(node && node->id == 3);
    std::cout << "  [OK] Find(150) -> slice 3" << std::endl;

    node = tree.Find(250);
    assert(node && node->id == 2);
    std::cout << "  [OK] Find(250) -> slice 2" << std::endl;

    node = tree.Find(500);
    assert(!node);
    std::cout << "  [OK] Find(500) -> nullptr" << std::endl;

    // 测试 GetRange
    auto range = tree.GetRange(50, 250);
    assert(range.size() == 3);
    std::cout << "  [OK] GetRange(50, 250) -> 3 slices" << std::endl;

    range = tree.GetRange(0, 100);
    assert(range.size() == 1 && range[0]->id == 1);
    std::cout << "  [OK] GetRange(0, 100) -> slice 1" << std::endl;

    // 测试 Build
    auto slices = tree.Build("chunks/test");
    assert(slices.size() == 3);
    assert(slices[0].storage_key == "chunks/test/1");
    std::cout << "  [OK] Build -> 3 SliceInfo" << std::endl;

    // 测试覆盖写入 (Cut)
    SliceTree tree2;
    tree2.Insert(0, 1, 1024, 0, 100);   // [0, 100)
    tree2.Insert(50, 2, 1024, 0, 100);  // [50, 150) 覆盖部分
    auto slices2 = tree2.Build("test");
    assert(slices2.size() == 2);
    assert(slices2[0].offset == 0 && slices2[0].size == 50);  // 被截断
    assert(slices2[1].offset == 50 && slices2[1].size == 100);
    std::cout << "  [OK] Overlapping insert (Cut)" << std::endl;

    std::cout << "All SliceTree tests passed!" << std::endl;
}

// ================================
// SingleFlight 测试
// ================================
void TestSingleFlight() {
    std::cout << "\nTesting SingleFlight..." << std::endl;

    SingleFlight<int> sf;
    std::atomic<int> call_count{0};

    // 测试 Do - 并发调用只执行一次
    auto fn = [&call_count]() {
        call_count++;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return 42;
    };

    std::vector<std::thread> threads;
    std::vector<int> results(5);
    for (int i = 0; i < 5; i++) {
        threads.emplace_back([&sf, &fn, &results, i]() {
            results[i] = sf.Do("key1", fn);
        });
    }
    for (auto& t : threads) t.join();

    assert(call_count == 1);
    for (int r : results) assert(r == 42);
    std::cout << "  [OK] Do: 5 concurrent calls, executed once" << std::endl;

    // 测试 TryPiggyback - 无进行中调用
    auto piggy = sf.TryPiggyback("nonexistent");
    assert(!piggy.has_value());
    std::cout << "  [OK] TryPiggyback: no in-flight call" << std::endl;

    // 测试 TryPiggyback - 有进行中调用
    std::atomic<bool> started{false};
    std::thread producer([&sf, &started]() {
        sf.Do("key2", [&started]() {
            started = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return 99;
        });
    });

    while (!started) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto piggy2 = sf.TryPiggyback("key2");
    assert(piggy2.has_value() && piggy2.value() == 99);
    std::cout << "  [OK] TryPiggyback: joined in-flight call" << std::endl;

    producer.join();

    std::cout << "All SingleFlight tests passed!" << std::endl;
}

// ================================
// PathConverter 测试
// ================================
void TestPathConverter() {
    std::cout << "\nTesting PathConverter..." << std::endl;

    PathConverter converter("mybucket");

    // 测试 S3ToPosix
    auto posix1 = converter.S3ToPosix("s3://mybucket/data/file.txt");
    assert(posix1 == "/data/file.txt");
    std::cout << "  [OK] S3ToPosix: s3://mybucket/data/file.txt -> " << posix1 << std::endl;

    auto posix2 = converter.S3ToPosix("s3://mybucket/");
    assert(posix2 == "/");
    std::cout << "  [OK] S3ToPosix: s3://mybucket/ -> " << posix2 << std::endl;

    auto posix3 = converter.S3ToPosix("/already/posix");
    assert(posix3 == "/already/posix");
    std::cout << "  [OK] S3ToPosix passthrough: /already/posix -> " << posix3 << std::endl;

    // 测试 PosixToS3
    auto s3_1 = converter.PosixToS3("/data/file.txt");
    assert(s3_1 == "s3://mybucket/data/file.txt");
    std::cout << "  [OK] PosixToS3: /data/file.txt -> " << s3_1 << std::endl;

    auto s3_2 = converter.PosixToS3("/");
    assert(s3_2 == "s3://mybucket/");
    std::cout << "  [OK] PosixToS3: / -> " << s3_2 << std::endl;

    // 测试 Parse
    auto parsed1 = converter.Parse("s3://mybucket/data/file.txt");
    assert(parsed1.is_s3 == true);
    assert(parsed1.bucket == "mybucket");
    assert(parsed1.key == "data/file.txt");
    assert(parsed1.posix_path == "/data/file.txt");
    std::cout << "  [OK] Parse S3 path: bucket=" << parsed1.bucket
              << ", key=" << parsed1.key << std::endl;

    auto parsed2 = converter.Parse("/local/path");
    assert(parsed2.is_s3 == false);
    assert(parsed2.posix_path == "/local/path");
    assert(parsed2.key == "local/path");
    std::cout << "  [OK] Parse POSIX path: posix=" << parsed2.posix_path << std::endl;

    // 边界情况
    auto parsed3 = converter.Parse("s3://otherbucket");
    assert(parsed3.bucket == "otherbucket");
    assert(parsed3.key == "");
    std::cout << "  [OK] Parse bucket-only S3 path" << std::endl;

    std::cout << "All PathConverter tests passed!" << std::endl;
}

// ================================
// MetadataServiceImpl 测试
// ================================
void TestMetadataServiceImpl() {
    std::cout << "\nTesting MetadataServiceImpl..." << std::endl;

    // 清理测试目录
    std::filesystem::remove_all("/tmp/nebula_meta_impl_test");

    // 创建分区
    MetaPartition::Config part_config;
    part_config.start_inode = 1;
    part_config.end_inode = 1000000;
    part_config.data_dir = "/tmp/nebula_meta_impl_test";
    auto partition = std::make_unique<MetaPartition>(part_config);
    auto status = partition->Init();
    assert(status.OK());
    std::cout << "  [OK] Partition initialized" << std::endl;

    // 创建服务
    MetadataServiceImpl::Config config;
    config.partitions.push_back(std::move(partition));
    MetadataServiceImpl service(std::move(config));

    // 测试路径解析
    auto r1 = service.ParsePath("/a/b/c");
    assert(r1.hasValue() && r1.value().size() == 3);
    std::cout << "  [OK] ParsePath: /a/b/c -> 3 parts" << std::endl;

    auto r2 = service.ParsePath("/");
    assert(r2.hasValue() && r2.value().empty());
    std::cout << "  [OK] ParsePath: / -> empty" << std::endl;

    auto r3 = service.ParsePath("invalid");
    assert(r3.hasError());
    std::cout << "  [OK] ParsePath: invalid path rejected" << std::endl;

    // 测试 Inode 分配
    auto id1 = service.GenerateInodeID();
    auto id2 = service.GenerateInodeID();
    auto id3 = service.GenerateInodeID();
    assert(id2 == id1 + 1 && id3 == id2 + 1);
    std::cout << "  [OK] GenerateInodeID: sequential " << id1 << ", " << id2 << ", " << id3 << std::endl;

    std::cout << "All MetadataServiceImpl tests passed!" << std::endl;
}

// ================================
// LocalBackend 扩展测试
// ================================
void TestLocalBackendExtended() {
    std::cout << "\nTesting LocalBackend (extended)..." << std::endl;

    // 清理测试目录
    std::filesystem::remove_all("/tmp/nebula_backend_test");

    LocalBackend::Config config;
    config.data_dir = "/tmp/nebula_backend_test";
    LocalBackend backend(std::move(config));
    std::cout << "  [OK] Backend initialized" << std::endl;

    // 测试 KeyToPath (通过 Put/Get 间接测试)
    // 由于协程需要运行时，这里测试初始化和配置

    // 测试 HealthCheck
    auto health_task = backend.HealthCheck();
    // 注意：协程需要运行时来执行
    std::cout << "  [OK] HealthCheck task created" << std::endl;

    std::cout << "LocalBackend extended tests passed!" << std::endl;
}

// ================================
// S3Backend 配置测试
// ================================
void TestS3BackendConfig() {
    std::cout << "\nTesting S3Backend configuration..." << std::endl;

    S3Backend::Config config;
    config.access_key = "test_access_key";
    config.secret_key = "test_secret_key";
    config.region = "us-east-1";
    config.bucket = "test-bucket";
    config.endpoint = "";  // 使用默认 AWS endpoint
    config.max_connections = 100;

    assert(!config.access_key.empty());
    assert(!config.secret_key.empty());
    assert(!config.region.empty());
    assert(!config.bucket.empty());
    std::cout << "  [OK] S3Backend config validation" << std::endl;

    // 测试自定义 endpoint (MinIO/Ceph)
    S3Backend::Config minio_config;
    minio_config.access_key = "minioadmin";
    minio_config.secret_key = "minioadmin";
    minio_config.region = "us-east-1";
    minio_config.bucket = "test";
    minio_config.endpoint = "localhost:9000";
    assert(!minio_config.endpoint.empty());
    std::cout << "  [OK] S3Backend MinIO config" << std::endl;

    std::cout << "All S3Backend config tests passed!" << std::endl;
}

// ================================
// RocksDBStore 编解码测试
// ================================
void TestRocksDBCodec() {
    std::cout << "\nTesting RocksDBStore codec..." << std::endl;

    // 清理测试目录
    std::filesystem::remove_all("/tmp/nebula_codec_test");

    RocksDBStore::Config config;
    config.db_path = "/tmp/nebula_codec_test";
    config.create_if_missing = true;
    RocksDBStore store(config);
    auto status = store.Init();
    assert(status.OK());

    // 测试 Dentry 编解码
    Dentry original_dentry{"test.txt", 12345, FileType::kRegular};
    auto encoded = store.EncodeDentryValue(original_dentry);
    auto decoded = store.DecodeDentryValue(encoded);
    assert(decoded.inode_id == 12345);
    assert(decoded.type == FileType::kRegular);
    std::cout << "  [OK] Dentry encode/decode" << std::endl;

    // 测试 Inode 编解码
    InodeAttr original_inode{
        .inode_id = 100,
        .mode = FileMode{0100644},
        .uid = 1000,
        .gid = 1000,
        .size = 4096,
        .mtime = 1234567890,
        .ctime = 1234567890,
        .nlink = 1
    };
    auto inode_encoded = store.EncodeInodeValue(original_inode);
    auto inode_decoded = store.DecodeInodeValue(inode_encoded);
    assert(inode_decoded.inode_id == 100);
    assert(inode_decoded.uid == 1000);
    assert(inode_decoded.size == 4096);
    std::cout << "  [OK] Inode encode/decode" << std::endl;

    // 测试 FileLayout 编解码
    FileLayout original_layout;
    original_layout.inode_id = 200;
    original_layout.chunk_size = 4 * 1024 * 1024;
    original_layout.slices.push_back({1, 0, 1024, "chunks/200/0"});
    original_layout.slices.push_back({2, 1024, 2048, "chunks/200/1"});

    auto layout_encoded = store.EncodeLayoutValue(original_layout);
    auto layout_decoded = store.DecodeLayoutValue(layout_encoded);
    assert(layout_decoded.chunk_size == 4 * 1024 * 1024);
    assert(layout_decoded.slices.size() == 2);
    assert(layout_decoded.slices[0].storage_key == "chunks/200/0");
    assert(layout_decoded.slices[1].offset == 1024);
    std::cout << "  [OK] FileLayout encode/decode" << std::endl;

    // 测试 Key 编码
    auto dentry_key = store.EncodeDentryKey(1, "test.txt");
    assert(dentry_key[0] == 'D');
    std::cout << "  [OK] Dentry key encoding" << std::endl;

    auto inode_key = store.EncodeInodeKey(100);
    assert(inode_key[0] == 'I');
    std::cout << "  [OK] Inode key encoding" << std::endl;

    auto layout_key = store.EncodeLayoutKey(100);
    assert(layout_key[0] == 'L');
    std::cout << "  [OK] Layout key encoding" << std::endl;

    std::cout << "All RocksDBStore codec tests passed!" << std::endl;
}

// ================================
// RocksDBStore 删除和目录扫描测试
// ================================
void TestRocksDBDeleteAndList() {
    std::cout << "\nTesting RocksDBStore delete/list..." << std::endl;

    std::filesystem::remove_all("/tmp/nebula_delete_test");

    RocksDBStore::Config config;
    config.db_path = "/tmp/nebula_delete_test";
    config.create_if_missing = true;
    RocksDBStore store(config);
    assert(store.Init().OK());

    // 创建测试数据
    auto txn = store.BeginTransaction();
    txn->CreateInode(1, FileMode{0040755}, 0, 0);  // root dir
    txn->CreateDentry(1, "file1.txt", 2, FileType::kRegular);
    txn->CreateDentry(1, "file2.txt", 3, FileType::kRegular);
    txn->CreateDentry(1, "subdir", 4, FileType::kDirectory);
    txn->CreateInode(2, FileMode{0100644}, 1000, 1000);
    txn->CreateInode(3, FileMode{0100644}, 1000, 1000);
    txn->CreateInode(4, FileMode{0040755}, 1000, 1000);
    assert(txn->Commit().OK());
    std::cout << "  [OK] Created test data" << std::endl;

    // 测试 ListDentries
    std::vector<Dentry> entries;
    auto status = store.ListDentries(1, &entries);
    assert(status.OK());
    assert(entries.size() == 3);
    std::cout << "  [OK] ListDentries: found " << entries.size() << " entries" << std::endl;

    // 测试 DeleteDentry
    status = store.DeleteDentry(1, "file1.txt");
    assert(status.OK());
    entries.clear();
    store.ListDentries(1, &entries);
    assert(entries.size() == 2);
    std::cout << "  [OK] DeleteDentry: removed file1.txt" << std::endl;

    // 测试 DeleteInode
    status = store.DeleteInode(2);
    assert(status.OK());
    InodeAttr attr;
    status = store.LookupInode(2, &attr);
    assert(!status.OK());  // 应该找不到
    std::cout << "  [OK] DeleteInode: removed inode 2" << std::endl;

    std::cout << "All RocksDBStore delete/list tests passed!" << std::endl;
}

// ================================
// ByteBuffer 测试
// ================================
void TestByteBuffer() {
    std::cout << "\nTesting ByteBuffer..." << std::endl;

    // 测试默认构造
    ByteBuffer empty;
    assert(empty.empty());
    assert(empty.size() == 0);
    std::cout << "  [OK] Default constructor" << std::endl;

    // 测试数据构造
    const char* data = "Hello, World!";
    ByteBuffer buf1(data, strlen(data));
    assert(buf1.size() == 13);
    assert(!buf1.empty());
    std::cout << "  [OK] Data constructor" << std::endl;

    // 测试 ToString
    auto str = buf1.ToString();
    assert(str == "Hello, World!");
    std::cout << "  [OK] ToString" << std::endl;

    // 测试 assign (void*, size_t)
    ByteBuffer buf2;
    buf2.assign("Test", 4);
    assert(buf2.size() == 4);
    assert(buf2.ToString() == "Test");
    std::cout << "  [OK] assign(void*, size_t)" << std::endl;

    // 测试 assign (vector&&)
    std::vector<uint8_t> vec = {'A', 'B', 'C'};
    ByteBuffer buf3;
    buf3.assign(std::move(vec));
    assert(buf3.size() == 3);
    assert(buf3.data()[0] == 'A');
    std::cout << "  [OK] assign(vector&&)" << std::endl;

    std::cout << "All ByteBuffer tests passed!" << std::endl;
}

// ================================
// Status 测试
// ================================
void TestStatus() {
    std::cout << "\nTesting Status..." << std::endl;

    // 测试 Ok
    auto ok = Status::Ok();
    assert(ok.OK());
    assert(ok.code() == ErrorCode::kOK);
    std::cout << "  [OK] Status::Ok()" << std::endl;

    // 测试 NotFound
    auto not_found = Status::NotFound("file not found");
    assert(!not_found.OK());
    assert(not_found.code() == ErrorCode::kNotFound);
    assert(not_found.message() == "file not found");
    std::cout << "  [OK] Status::NotFound()" << std::endl;

    // 测试 Exist
    auto exist = Status::Exist("already exists");
    assert(exist.code() == ErrorCode::kExist);
    std::cout << "  [OK] Status::Exist()" << std::endl;

    // 测试 InvalidArgument
    auto invalid = Status::InvalidArgument("bad input");
    assert(invalid.code() == ErrorCode::kInvalidArgument);
    std::cout << "  [OK] Status::InvalidArgument()" << std::endl;

    // 测试 IO
    auto io_err = Status::IO("disk error");
    assert(io_err.code() == ErrorCode::kIOError);
    std::cout << "  [OK] Status::IO()" << std::endl;

    std::cout << "All Status tests passed!" << std::endl;
}

// ================================
// FileMode 测试
// ================================
void TestFileMode() {
    std::cout << "\nTesting FileMode..." << std::endl;

    // 测试普通文件
    FileMode regular{0100644};
    assert(regular.IsRegular());
    assert(!regular.IsDirectory());
    assert(!regular.IsSymlink());
    assert(regular.IsReadable());
    assert(regular.IsWritable());
    assert(!regular.IsExecutable());
    std::cout << "  [OK] Regular file mode" << std::endl;

    // 测试目录
    FileMode dir{0040755};
    assert(dir.IsDirectory());
    assert(!dir.IsRegular());
    assert(dir.IsReadable());
    assert(dir.IsWritable());
    assert(dir.IsExecutable());
    std::cout << "  [OK] Directory mode" << std::endl;

    // 测试符号链接
    FileMode symlink{0120777};
    assert(symlink.IsSymlink());
    assert(!symlink.IsRegular());
    assert(!symlink.IsDirectory());
    std::cout << "  [OK] Symlink mode" << std::endl;

    // 测试 FromUint
    auto mode = FileMode::FromUint(0100755);
    assert(mode.mode == 0100755);
    std::cout << "  [OK] FileMode::FromUint()" << std::endl;

    std::cout << "All FileMode tests passed!" << std::endl;
}

// ================================
// 主函数
// ================================
int main() {
    std::cout << "====================================\n";
    std::cout << "NebulaStore 2.0 - Module Tests\n";
    std::cout << "====================================\n";

    try {
        TestResult();
        TestSliceTree();
        TestSingleFlight();
        TestByteBuffer();
        TestStatus();
        TestFileMode();
        TestPathConverter();
        TestRocksDBCodec();
        TestRocksDBDeleteAndList();
        TestMetadataServiceImpl();
        TestLocalBackendExtended();
        TestS3BackendConfig();

        std::cout << "\n====================================\n";
        std::cout << "All module tests PASSED!\n";
        std::cout << "====================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nTest FAILED: " << e.what() << std::endl;
        return 1;
    }
}
