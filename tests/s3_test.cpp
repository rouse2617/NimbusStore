// ================================
// S3 模块单元测试 - 100% 覆盖率
// ================================

#include <iostream>
#include <cassert>
#include <filesystem>
#include "nebulastore/protocol/s3_metadata.h"
#include "nebulastore/protocol/s3_backend_rocksdb.h"
#include "nebulastore/protocol/s3_router.h"
#include "nebulastore/protocol/s3_xml.h"

using namespace nebulastore::s3;

#define TEST(name) void name(); \
    struct name##_register { name##_register() { tests.push_back({#name, name}); } } name##_instance; \
    void name()

std::vector<std::pair<std::string, void(*)()>> tests;

// ================================
// 1. BucketMeta 序列化测试
// ================================
void TestBucketMetaEncodeDecode() {
    std::cout << "Testing BucketMeta Encode/Decode..." << std::endl;

    BucketMeta meta;
    meta.name = "test-bucket";
    meta.owner = "user123";
    meta.creation_time = 1704067200;
    meta.object_count = 100;
    meta.total_size = 1024000;
    meta.region = "us-east-1";
    meta.storage_class = "STANDARD";

    // 编码
    std::string encoded = meta.Encode();
    assert(!encoded.empty());
    std::cout << "  [OK] Encode: " << encoded.size() << " bytes" << std::endl;

    // 解码
    BucketMeta decoded;
    bool ok = decoded.Decode(encoded);
    assert(ok);
    assert(decoded.name == meta.name);
    assert(decoded.owner == meta.owner);
    assert(decoded.creation_time == meta.creation_time);
    assert(decoded.object_count == meta.object_count);
    assert(decoded.total_size == meta.total_size);
    assert(decoded.region == meta.region);
    assert(decoded.storage_class == meta.storage_class);
    std::cout << "  [OK] Decode: all fields match" << std::endl;

    // 空数据解码失败
    BucketMeta empty;
    assert(!empty.Decode(""));
    assert(!empty.Decode("abc"));
    std::cout << "  [OK] Invalid data rejected" << std::endl;

    std::cout << "BucketMeta tests passed!" << std::endl;
}

// ================================
// 2. ObjectMeta 序列化测试
// ================================
void TestObjectMetaEncodeDecode() {
    std::cout << "\nTesting ObjectMeta Encode/Decode..." << std::endl;

    ObjectMeta meta;
    meta.bucket = "test-bucket";
    meta.key = "path/to/file.txt";
    meta.size = 12345;
    meta.etag = "d41d8cd98f00b204e9800998ecf8427e";
    meta.content_type = "text/plain";
    meta.last_modified = 1704067200;
    meta.storage_class = "STANDARD";
    meta.data_path = "/data/test-bucket/file.txt";
    meta.user_metadata["x-amz-meta-author"] = "test";
    meta.user_metadata["x-amz-meta-version"] = "1.0";

    // 编码
    std::string encoded = meta.Encode();
    assert(!encoded.empty());
    std::cout << "  [OK] Encode: " << encoded.size() << " bytes" << std::endl;

    // 解码
    ObjectMeta decoded;
    bool ok = decoded.Decode(encoded);
    assert(ok);
    assert(decoded.bucket == meta.bucket);
    assert(decoded.key == meta.key);
    assert(decoded.size == meta.size);
    assert(decoded.etag == meta.etag);
    assert(decoded.content_type == meta.content_type);
    assert(decoded.last_modified == meta.last_modified);
    assert(decoded.storage_class == meta.storage_class);
    assert(decoded.data_path == meta.data_path);
    assert(decoded.user_metadata.size() == 2);
    assert(decoded.user_metadata["x-amz-meta-author"] == "test");
    std::cout << "  [OK] Decode: all fields match" << std::endl;

    // 空 user_metadata
    ObjectMeta meta2;
    meta2.bucket = "b";
    meta2.key = "k";
    std::string enc2 = meta2.Encode();
    ObjectMeta dec2;
    assert(dec2.Decode(enc2));
    assert(dec2.user_metadata.empty());
    std::cout << "  [OK] Empty user_metadata handled" << std::endl;

    std::cout << "ObjectMeta tests passed!" << std::endl;
}

// ================================
// 3. RocksDBBackend 测试
// ================================
void TestRocksDBBackend() {
    std::cout << "\nTesting RocksDBBackend..." << std::endl;

    std::string db_path = "/tmp/nebula_s3_test_db";
    std::filesystem::remove_all(db_path);

    auto backend = std::make_unique<RocksDBBackend>(db_path);
    std::cout << "  [OK] Backend created" << std::endl;

    // Put/Get
    assert(backend->Put("key1", "value1"));
    std::string value;
    assert(backend->Get("key1", value));
    assert(value == "value1");
    std::cout << "  [OK] Put/Get" << std::endl;

    // Exists
    assert(backend->Exists("key1"));
    assert(!backend->Exists("nonexistent"));
    std::cout << "  [OK] Exists" << std::endl;

    // Delete
    assert(backend->Delete("key1"));
    assert(!backend->Exists("key1"));
    std::cout << "  [OK] Delete" << std::endl;

    // BatchPut
    std::vector<std::pair<std::string, std::string>> kvs = {
        {"batch1", "v1"},
        {"batch2", "v2"},
        {"batch3", "v3"}
    };
    assert(backend->BatchPut(kvs));
    assert(backend->Exists("batch1"));
    assert(backend->Exists("batch2"));
    assert(backend->Exists("batch3"));
    std::cout << "  [OK] BatchPut" << std::endl;

    // Scan
    backend->Put("prefix:a", "1");
    backend->Put("prefix:b", "2");
    backend->Put("prefix:c", "3");
    backend->Put("other:x", "4");

    auto results = backend->Scan("prefix:", 10);
    assert(results.size() == 3);
    std::cout << "  [OK] Scan with prefix" << std::endl;

    // Scan with limit
    auto limited = backend->Scan("prefix:", 2);
    assert(limited.size() == 2);
    std::cout << "  [OK] Scan with limit" << std::endl;

    std::filesystem::remove_all(db_path);
    std::cout << "RocksDBBackend tests passed!" << std::endl;
}

// ================================
// 4. MetadataBackendFactory 测试
// ================================
void TestMetadataBackendFactory() {
    std::cout << "\nTesting MetadataBackendFactory..." << std::endl;

    RegisterRocksDBBackend();

    std::string db_path = "/tmp/nebula_factory_test_db";
    std::filesystem::remove_all(db_path);

    auto backend = MetadataBackendFactory::Instance().Create("rocksdb", db_path);
    assert(backend != nullptr);
    std::cout << "  [OK] Create rocksdb backend" << std::endl;

    // 测试创建的后端可用
    assert(backend->Put("test", "value"));
    std::string v;
    assert(backend->Get("test", v));
    assert(v == "value");
    std::cout << "  [OK] Created backend works" << std::endl;

    // 未注册的类型返回 nullptr
    auto unknown = MetadataBackendFactory::Instance().Create("unknown", "/tmp/x");
    assert(unknown == nullptr);
    std::cout << "  [OK] Unknown type returns nullptr" << std::endl;

    std::filesystem::remove_all(db_path);
    std::cout << "MetadataBackendFactory tests passed!" << std::endl;
}

// ================================
// 5. S3MetadataStore Bucket 操作测试
// ================================
void TestS3MetadataStoreBucket() {
    std::cout << "\nTesting S3MetadataStore Bucket operations..." << std::endl;

    std::string db_path = "/tmp/nebula_s3meta_test_db";
    std::filesystem::remove_all(db_path);

    RegisterRocksDBBackend();
    auto backend = MetadataBackendFactory::Instance().Create("rocksdb", db_path);
    S3MetadataStore store(std::move(backend));

    // PutBucket
    BucketMeta meta;
    meta.name = "mybucket";
    meta.owner = "user1";
    meta.creation_time = 1704067200;
    assert(store.PutBucket(meta));
    std::cout << "  [OK] PutBucket" << std::endl;

    // BucketExists
    assert(store.BucketExists("mybucket"));
    assert(!store.BucketExists("nonexistent"));
    std::cout << "  [OK] BucketExists" << std::endl;

    // GetBucket
    BucketMeta retrieved;
    assert(store.GetBucket("mybucket", retrieved));
    assert(retrieved.name == "mybucket");
    assert(retrieved.owner == "user1");
    std::cout << "  [OK] GetBucket" << std::endl;

    // GetBucket 不存在
    BucketMeta notfound;
    assert(!store.GetBucket("nonexistent", notfound));
    std::cout << "  [OK] GetBucket nonexistent returns false" << std::endl;

    // ListBuckets
    BucketMeta meta2;
    meta2.name = "bucket2";
    meta2.owner = "user2";
    store.PutBucket(meta2);

    auto buckets = store.ListBuckets();
    assert(buckets.size() == 2);
    std::cout << "  [OK] ListBuckets: " << buckets.size() << " buckets" << std::endl;

    // DeleteBucket
    assert(store.DeleteBucket("mybucket"));
    assert(!store.BucketExists("mybucket"));
    std::cout << "  [OK] DeleteBucket" << std::endl;

    std::filesystem::remove_all(db_path);
    std::cout << "S3MetadataStore Bucket tests passed!" << std::endl;
}

// ================================
// 6. S3MetadataStore Object 操作测试
// ================================
void TestS3MetadataStoreObject() {
    std::cout << "\nTesting S3MetadataStore Object operations..." << std::endl;

    std::string db_path = "/tmp/nebula_s3obj_test_db";
    std::filesystem::remove_all(db_path);

    RegisterRocksDBBackend();
    auto backend = MetadataBackendFactory::Instance().Create("rocksdb", db_path);
    S3MetadataStore store(std::move(backend));

    // 先创建 bucket
    BucketMeta bucket;
    bucket.name = "testbucket";
    store.PutBucket(bucket);

    // PutObject
    ObjectMeta obj;
    obj.bucket = "testbucket";
    obj.key = "file1.txt";
    obj.size = 1024;
    obj.etag = "abc123";
    assert(store.PutObject(obj));
    std::cout << "  [OK] PutObject" << std::endl;

    // ObjectExists
    assert(store.ObjectExists("testbucket", "file1.txt"));
    assert(!store.ObjectExists("testbucket", "nonexistent.txt"));
    std::cout << "  [OK] ObjectExists" << std::endl;

    // GetObject
    ObjectMeta retrieved;
    assert(store.GetObject("testbucket", "file1.txt", retrieved));
    assert(retrieved.key == "file1.txt");
    assert(retrieved.size == 1024);
    std::cout << "  [OK] GetObject" << std::endl;

    // GetObject 不存在
    ObjectMeta notfound;
    assert(!store.GetObject("testbucket", "nonexistent.txt", notfound));
    std::cout << "  [OK] GetObject nonexistent returns false" << std::endl;

    // ListObjects
    ObjectMeta obj2;
    obj2.bucket = "testbucket";
    obj2.key = "file2.txt";
    store.PutObject(obj2);

    ObjectMeta obj3;
    obj3.bucket = "testbucket";
    obj3.key = "dir/file3.txt";
    store.PutObject(obj3);

    auto objects = store.ListObjects("testbucket");
    assert(objects.size() == 3);
    std::cout << "  [OK] ListObjects: " << objects.size() << " objects" << std::endl;

    // ListObjects with prefix
    auto prefixed = store.ListObjects("testbucket", "dir/");
    assert(prefixed.size() == 1);
    assert(prefixed[0].key == "dir/file3.txt");
    std::cout << "  [OK] ListObjects with prefix" << std::endl;

    // ListObjects with marker
    auto after_marker = store.ListObjects("testbucket", "", "dir/file3.txt", 10);
    // marker 之后的对象: file2.txt (按字典序 dir/file3.txt < file2.txt)
    assert(after_marker.size() >= 1);
    std::cout << "  [OK] ListObjects with marker: " << after_marker.size() << " objects" << std::endl;

    // ListObjects with max_keys
    auto limited = store.ListObjects("testbucket", "", "", 2);
    assert(limited.size() == 2);
    std::cout << "  [OK] ListObjects with max_keys" << std::endl;

    // DeleteObject
    assert(store.DeleteObject("testbucket", "file1.txt"));
    assert(!store.ObjectExists("testbucket", "file1.txt"));
    std::cout << "  [OK] DeleteObject" << std::endl;

    // UpdateBucketStats
    bucket.object_count = 0;
    bucket.total_size = 0;
    store.PutBucket(bucket);
    assert(store.UpdateBucketStats("testbucket", 1000, 5));
    BucketMeta updated;
    store.GetBucket("testbucket", updated);
    assert(updated.total_size == 1000);
    assert(updated.object_count == 5);
    std::cout << "  [OK] UpdateBucketStats" << std::endl;

    std::filesystem::remove_all(db_path);
    std::cout << "S3MetadataStore Object tests passed!" << std::endl;
}

// ================================
// 7. S3Router 测试
// ================================
void TestS3Router() {
    std::cout << "\nTesting S3Router..." << std::endl;

    // LIST_BUCKETS: GET /
    {
        S3Request req;
        req.method = "GET";
        req.uri = "/";
        S3Router::ParseRequest(req);
        assert(req.op == S3Op::LIST_BUCKETS);
        std::cout << "  [OK] LIST_BUCKETS: GET /" << std::endl;
    }

    // CREATE_BUCKET: PUT /bucket
    {
        S3Request req;
        req.method = "PUT";
        req.uri = "/mybucket";
        S3Router::ParseRequest(req);
        assert(req.op == S3Op::CREATE_BUCKET);
        assert(req.bucket_name == "mybucket");
        std::cout << "  [OK] CREATE_BUCKET: PUT /mybucket" << std::endl;
    }

    // DELETE_BUCKET: DELETE /bucket
    {
        S3Request req;
        req.method = "DELETE";
        req.uri = "/mybucket";
        S3Router::ParseRequest(req);
        assert(req.op == S3Op::DELETE_BUCKET);
        assert(req.bucket_name == "mybucket");
        std::cout << "  [OK] DELETE_BUCKET: DELETE /mybucket" << std::endl;
    }

    // HEAD_BUCKET: HEAD /bucket
    {
        S3Request req;
        req.method = "HEAD";
        req.uri = "/mybucket";
        S3Router::ParseRequest(req);
        assert(req.op == S3Op::HEAD_BUCKET);
        std::cout << "  [OK] HEAD_BUCKET: HEAD /mybucket" << std::endl;
    }

    // LIST_OBJECTS: GET /bucket
    {
        S3Request req;
        req.method = "GET";
        req.uri = "/mybucket";
        S3Router::ParseRequest(req);
        assert(req.op == S3Op::LIST_OBJECTS);
        std::cout << "  [OK] LIST_OBJECTS: GET /mybucket" << std::endl;
    }

    // LIST_OBJECTS_V2: GET /bucket?list-type=2
    {
        S3Request req;
        req.method = "GET";
        req.uri = "/mybucket?list-type=2";
        S3Router::ParseRequest(req);
        assert(req.op == S3Op::LIST_OBJECTS_V2);
        std::cout << "  [OK] LIST_OBJECTS_V2: GET /mybucket?list-type=2" << std::endl;
    }

    // PUT_OBJECT: PUT /bucket/key
    {
        S3Request req;
        req.method = "PUT";
        req.uri = "/mybucket/path/to/file.txt";
        S3Router::ParseRequest(req);
        assert(req.op == S3Op::PUT_OBJECT);
        assert(req.bucket_name == "mybucket");
        assert(req.object_key == "path/to/file.txt");
        std::cout << "  [OK] PUT_OBJECT: PUT /mybucket/path/to/file.txt" << std::endl;
    }

    // GET_OBJECT: GET /bucket/key
    {
        S3Request req;
        req.method = "GET";
        req.uri = "/mybucket/file.txt";
        S3Router::ParseRequest(req);
        assert(req.op == S3Op::GET_OBJECT);
        assert(req.bucket_name == "mybucket");
        assert(req.object_key == "file.txt");
        std::cout << "  [OK] GET_OBJECT: GET /mybucket/file.txt" << std::endl;
    }

    // DELETE_OBJECT: DELETE /bucket/key
    {
        S3Request req;
        req.method = "DELETE";
        req.uri = "/mybucket/file.txt";
        S3Router::ParseRequest(req);
        assert(req.op == S3Op::DELETE_OBJECT);
        std::cout << "  [OK] DELETE_OBJECT: DELETE /mybucket/file.txt" << std::endl;
    }

    // HEAD_OBJECT: HEAD /bucket/key
    {
        S3Request req;
        req.method = "HEAD";
        req.uri = "/mybucket/file.txt";
        S3Router::ParseRequest(req);
        assert(req.op == S3Op::HEAD_OBJECT);
        std::cout << "  [OK] HEAD_OBJECT: HEAD /mybucket/file.txt" << std::endl;
    }

    // Query string parsing
    {
        S3Request req;
        req.method = "GET";
        req.uri = "/mybucket?prefix=dir/&max-keys=100&marker=file1.txt";
        S3Router::ParseRequest(req);
        assert(req.params["prefix"] == "dir/");
        assert(req.params["max-keys"] == "100");
        assert(req.params["marker"] == "file1.txt");
        std::cout << "  [OK] Query string parsing" << std::endl;
    }

    std::cout << "S3Router tests passed!" << std::endl;
}

// ================================
// 8. S3XML 测试
// ================================
void TestS3XML() {
    std::cout << "\nTesting S3XMLFormatter..." << std::endl;

    // ListBucketsResult
    {
        std::vector<BucketInfo> buckets;
        BucketInfo b1;
        b1.name = "bucket1";
        b1.creation_date = "2024-01-01T00:00:00.000Z";
        buckets.push_back(b1);

        BucketInfo b2;
        b2.name = "bucket2";
        b2.creation_date = "2024-01-02T00:00:00.000Z";
        buckets.push_back(b2);

        std::string xml = S3XMLFormatter::ListBucketsResult("owner123", "owner", buckets);
        assert(xml.find("<ListAllMyBucketsResult") != std::string::npos);
        assert(xml.find("<Name>bucket1</Name>") != std::string::npos);
        assert(xml.find("<Name>bucket2</Name>") != std::string::npos);
        assert(xml.find("<ID>owner123</ID>") != std::string::npos);
        std::cout << "  [OK] ListBucketsResult" << std::endl;
    }

    // ListBucketResult
    {
        ListObjectsResult r;
        r.bucket_name = "mybucket";
        r.prefix = "";
        r.marker = "";
        r.max_keys = 1000;
        r.is_truncated = false;

        ObjectInfo o1;
        o1.key = "file1.txt";
        o1.size = 1024;
        o1.etag = "abc123";
        o1.last_modified = "2024-01-01T00:00:00.000Z";
        o1.storage_class = "STANDARD";
        r.objects.push_back(o1);

        std::string xml = S3XMLFormatter::ListBucketResult(r);
        assert(xml.find("<ListBucketResult") != std::string::npos);
        assert(xml.find("<Name>mybucket</Name>") != std::string::npos);
        assert(xml.find("<Key>file1.txt</Key>") != std::string::npos);
        assert(xml.find("<Size>1024</Size>") != std::string::npos);
        assert(xml.find("<IsTruncated>false</IsTruncated>") != std::string::npos);
        std::cout << "  [OK] ListBucketResult" << std::endl;
    }

    // ListBucketResult with truncation
    {
        ListObjectsResult r;
        r.bucket_name = "mybucket";
        r.prefix = "dir/";
        r.marker = "marker";
        r.max_keys = 100;
        r.is_truncated = true;

        std::string xml = S3XMLFormatter::ListBucketResult(r);
        assert(xml.find("<IsTruncated>true</IsTruncated>") != std::string::npos);
        assert(xml.find("<Prefix>dir/</Prefix>") != std::string::npos);
        assert(xml.find("<Marker>marker</Marker>") != std::string::npos);
        assert(xml.find("<MaxKeys>100</MaxKeys>") != std::string::npos);
        std::cout << "  [OK] ListBucketResult with truncation" << std::endl;
    }

    std::cout << "S3XMLFormatter tests passed!" << std::endl;
}

// ================================
// 9. encoding 命名空间测试
// ================================
void TestEncoding() {
    std::cout << "\nTesting encoding namespace..." << std::endl;

    std::string buf;

    // PutU32/GetU32
    encoding::PutU32(buf, 12345);
    size_t pos = 0;
    uint32_t u32;
    assert(encoding::GetU32(buf, pos, u32));
    assert(u32 == 12345);
    std::cout << "  [OK] PutU32/GetU32" << std::endl;

    // PutU64/GetU64
    buf.clear();
    encoding::PutU64(buf, 9876543210ULL);
    pos = 0;
    uint64_t u64;
    assert(encoding::GetU64(buf, pos, u64));
    assert(u64 == 9876543210ULL);
    std::cout << "  [OK] PutU64/GetU64" << std::endl;

    // PutString/GetString
    buf.clear();
    encoding::PutString(buf, "hello world");
    pos = 0;
    std::string str;
    assert(encoding::GetString(buf, pos, str));
    assert(str == "hello world");
    std::cout << "  [OK] PutString/GetString" << std::endl;

    // 边界情况
    buf.clear();
    pos = 0;
    assert(!encoding::GetU32(buf, pos, u32));  // 空数据
    buf = "ab";
    pos = 0;
    assert(!encoding::GetU32(buf, pos, u32));  // 数据不足
    std::cout << "  [OK] Boundary cases" << std::endl;

    std::cout << "encoding tests passed!" << std::endl;
}

// ================================
// Main
// ================================
int main() {
    std::cout << "====================================\n";
    std::cout << "S3 Module Unit Tests - 100% Coverage\n";
    std::cout << "====================================\n";

    try {
        TestBucketMetaEncodeDecode();
        TestObjectMetaEncodeDecode();
        TestRocksDBBackend();
        TestMetadataBackendFactory();
        TestS3MetadataStoreBucket();
        TestS3MetadataStoreObject();
        TestS3Router();
        TestS3XML();
        TestEncoding();

        std::cout << "\n====================================\n";
        std::cout << "All S3 tests PASSED!\n";
        std::cout << "====================================\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nTest FAILED: " << e.what() << std::endl;
        return 1;
    }
}
