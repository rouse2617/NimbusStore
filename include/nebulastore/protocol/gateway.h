#pragma once

#include <memory>
#include <vector>
#include <map>
#include "nebulastore/common/types.h"
#include "nebulastore/common/async.h"
#include "nebulastore/metadata/metadata_service.h"
#include "nebulastore/storage/backend.h"
#include "nebulastore/namespace/service.h"

namespace yig::protocol {

// ================================
// S3 对象 (用于 ListObjects 响应)
// ================================

struct S3Object {
    std::string key;
    uint64_t size;
    uint64_t mtime;
    std::string etag;
};

// ================================
// S3 Gateway (CubeFS ObjectNode 无状态设计)
// ================================

class S3Gateway {
public:
    struct Config {
        std::shared_ptr<namespace_::NamespaceService> namespace_service;
        std::string host = "0.0.0.0";
        uint16_t port = 8080;
        uint32_t num_threads = 16;
    };

    explicit S3Gateway(Config config);
    ~S3Gateway();

    // === 启动/停止 ===

    Status Start();
    void Stop();
    void Join();

    // === S3 API 实现 ===

    // PUT Object
    AsyncTask<Status> PutObject(
        const std::string& bucket,
        const std::string& key,
        const ByteBuffer& data,
        const std::map<std::string, std::string>& metadata = {}
    );

    // GET Object
    AsyncTask<Status> GetObject(
        const std::string& bucket,
        const std::string& key,
        ByteBuffer* data,
        uint64_t offset = 0,
        uint64_t size = 0  // 0 表示全部
    );

    // HEAD Object
    AsyncTask<Status> HeadObject(
        const std::string& bucket,
        const std::string& key,
        InodeAttr* attr
    );

    // DELETE Object
    AsyncTask<Status> DeleteObject(
        const std::string& bucket,
        const std::string& key
    );

    // ListObjects
    AsyncTask<Status> ListObjects(
        const std::string& bucket,
        const std::string& prefix,
        std::vector<S3Object>* objects
    );

    // CreateMultipartUpload (分片上传)
    AsyncTask<Status> CreateMultipartUpload(
        const std::string& bucket,
        const std::string& key,
        std::string* upload_id
    );

    // UploadPart
    AsyncTask<Status> UploadPart(
        const std::string& bucket,
        const std::string& key,
        const std::string& upload_id,
        int part_number,
        const ByteBuffer& data
    );

    // CompleteMultipartUpload
    AsyncTask<Status> CompleteMultipartUpload(
        const std::string& bucket,
        const std::string& key,
        const std::string& upload_id
    );

private:
    Config config_;

    class HTTPServer;
    std::unique_ptr<HTTPServer> server_;
};

// ================================
// POSIX Client (FUSE)
// ================================

class FuseClient {
public:
    struct Config {
        std::shared_ptr<namespace_::NamespaceService> namespace_service;
        std::string mount_point;
        uint32_t max_readahead = 131072;  // 128KB
        bool allow_other = false;
    };

    explicit FuseClient(Config config);
    ~FuseClient();

    // === 挂载/卸载 ===

    Status Mount();
    void Unmount();
    void Join();

    // === FUSE 操作实现 ===

    AsyncTask<Status> GetAttr(
        const std::string& path,
        InodeAttr* attr
    );

    AsyncTask<Status> SetAttr(
        const std::string& path,
        const InodeAttr& attr,
        uint32_t to_set
    );

    AsyncTask<Status> Read(
        const std::string& path,
        uint64_t offset,
        uint64_t size,
        ByteBuffer* data
    );

    AsyncTask<Status> Write(
        const std::string& path,
        const ByteBuffer& data,
        uint64_t offset
    );

    AsyncTask<Status> Create(
        const std::string& path,
        FileMode mode,
        UserID uid,
        GroupID gid
    );

    AsyncTask<Status> Mkdir(
        const std::string& path,
        FileMode mode,
        UserID uid,
        GroupID gid
    );

    AsyncTask<Status> Unlink(
        const std::string& path
    );

    AsyncTask<Status> Rmdir(
        const std::string& path
    );

    AsyncTask<Status> Rename(
        const std::string& oldpath,
        const std::string& newpath
    );

    AsyncTask<Status> Readdir(
        const std::string& path,
        std::vector<Dentry>* entries
    );

private:
    Config config_;

    // 查找文件对应的 slice
    Status FindSlice(
        const FileLayout& layout,
        uint64_t offset,
        SliceInfo* slice
    );

    class FUSESession;
    std::unique_ptr<FUSESession> session_;
};

} // namespace yig::protocol
