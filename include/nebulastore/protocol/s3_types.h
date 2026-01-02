// ================================
// S3 API 类型定义 (参考 Ceph RGW)
// ================================

#pragma once

#include <string>
#include <vector>
#include <map>

namespace nebulastore {
namespace s3 {

// S3 操作类型
enum class S3Op {
    UNKNOWN,
    LIST_BUCKETS, CREATE_BUCKET, DELETE_BUCKET, HEAD_BUCKET,
    LIST_OBJECTS, LIST_OBJECTS_V2,
    GET_OBJECT, PUT_OBJECT, DELETE_OBJECT, HEAD_OBJECT, COPY_OBJECT,
    INIT_MULTIPART, UPLOAD_PART, COMPLETE_MULTIPART, ABORT_MULTIPART, LIST_PARTS,
};

// S3 错误码
struct S3Error {
    int http_status;
    std::string code;
    std::string message;

    static S3Error None() { return {200, "", ""}; }
    static S3Error AccessDenied() { return {403, "AccessDenied", "Access Denied"}; }
    static S3Error NoSuchBucket() { return {404, "NoSuchBucket", "The specified bucket does not exist"}; }
    static S3Error NoSuchKey() { return {404, "NoSuchKey", "The specified key does not exist"}; }
    static S3Error BucketAlreadyExists() { return {409, "BucketAlreadyExists", "Bucket already exists"}; }
    static S3Error BucketNotEmpty() { return {409, "BucketNotEmpty", "Bucket is not empty"}; }
    static S3Error InvalidArgument() { return {400, "InvalidArgument", "Invalid Argument"}; }
    static S3Error InternalError() { return {500, "InternalError", "Internal error"}; }
};

// S3 请求上下文
struct S3Request {
    std::string method;
    std::string uri;
    std::string query_string;
    std::map<std::string, std::string> headers;
    std::string body;
    std::string bucket_name;
    std::string object_key;
    S3Op op = S3Op::UNKNOWN;
    std::map<std::string, std::string> params;

    std::string GetHeader(const std::string& name) const {
        auto it = headers.find(name);
        return it != headers.end() ? it->second : "";
    }
    std::string GetParam(const std::string& name) const {
        auto it = params.find(name);
        return it != params.end() ? it->second : "";
    }
};

// S3 响应
struct S3Response {
    int status_code = 200;
    std::map<std::string, std::string> headers;
    std::string body;
    std::string content_type = "application/xml";

    void SetError(const S3Error& err) {
        status_code = err.http_status;
        if (!err.code.empty()) {
            body = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Error>\n  <Code>" + err.code + "</Code>\n  <Message>" + err.message + "</Message>\n</Error>";
        }
    }
};

struct BucketInfo {
    std::string name;
    std::string creation_date;
};

struct ObjectInfo {
    std::string key;
    std::string etag;
    uint64_t size = 0;
    std::string last_modified;
    std::string storage_class = "STANDARD";
};

struct ListObjectsResult {
    std::string bucket_name;
    std::string prefix;
    std::string marker;
    std::string delimiter;
    int max_keys = 1000;
    bool is_truncated = false;
    std::vector<ObjectInfo> objects;
    std::vector<std::string> common_prefixes;
};

} // namespace s3
} // namespace nebulastore
