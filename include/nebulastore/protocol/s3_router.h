// S3 路由器 (参考 Ceph RGW)
#pragma once

#include "nebulastore/protocol/s3_types.h"
#include <cstdio>

namespace nebulastore {
namespace s3 {

class S3Router {
public:
    static void ParseRequest(S3Request& req) {
        ParseUri(req);
        ParseQueryString(req);
        DetermineOperation(req);
    }

private:
    static void ParseUri(S3Request& req) {
        std::string path = req.uri;
        size_t query_pos = path.find('?');
        if (query_pos != std::string::npos) {
            req.query_string = path.substr(query_pos + 1);
            path = path.substr(0, query_pos);
        }
        if (!path.empty() && path[0] == '/') path = path.substr(1);
        
        if (path.empty()) {
            req.bucket_name = "";
            req.object_key = "";
        } else {
            size_t slash_pos = path.find('/');
            if (slash_pos == std::string::npos) {
                req.bucket_name = UrlDecode(path);
                req.object_key = "";
            } else {
                req.bucket_name = UrlDecode(path.substr(0, slash_pos));
                req.object_key = UrlDecode(path.substr(slash_pos + 1));
            }
        }
    }

    static void ParseQueryString(S3Request& req) {
        if (req.query_string.empty()) return;
        std::string& qs = req.query_string;
        size_t pos = 0;
        while (pos < qs.size()) {
            size_t eq_pos = qs.find('=', pos);
            size_t amp_pos = qs.find('&', pos);
            std::string key, value;
            if (eq_pos != std::string::npos && (amp_pos == std::string::npos || eq_pos < amp_pos)) {
                key = qs.substr(pos, eq_pos - pos);
                size_t val_end = (amp_pos != std::string::npos) ? amp_pos : qs.size();
                value = qs.substr(eq_pos + 1, val_end - eq_pos - 1);
            } else {
                size_t key_end = (amp_pos != std::string::npos) ? amp_pos : qs.size();
                key = qs.substr(pos, key_end - pos);
            }
            if (!key.empty()) req.params[UrlDecode(key)] = UrlDecode(value);
            if (amp_pos == std::string::npos) break;
            pos = amp_pos + 1;
        }
    }

    static void DetermineOperation(S3Request& req) {
        bool has_bucket = !req.bucket_name.empty();
        bool has_key = !req.object_key.empty();

        if (req.method == "GET") {
            if (!has_bucket) req.op = S3Op::LIST_BUCKETS;
            else if (!has_key) {
                req.op = S3Op::LIST_OBJECTS;
                if (req.params.count("list-type") && req.params["list-type"] == "2")
                    req.op = S3Op::LIST_OBJECTS_V2;
            } else req.op = S3Op::GET_OBJECT;
        } else if (req.method == "PUT") {
            if (!has_bucket) req.op = S3Op::UNKNOWN;
            else if (!has_key) req.op = S3Op::CREATE_BUCKET;
            else req.op = !req.GetHeader("x-amz-copy-source").empty() ? S3Op::COPY_OBJECT : S3Op::PUT_OBJECT;
        } else if (req.method == "DELETE") {
            if (!has_bucket) req.op = S3Op::UNKNOWN;
            else if (!has_key) req.op = S3Op::DELETE_BUCKET;
            else req.op = S3Op::DELETE_OBJECT;
        } else if (req.method == "HEAD") {
            if (!has_bucket) req.op = S3Op::UNKNOWN;
            else if (!has_key) req.op = S3Op::HEAD_BUCKET;
            else req.op = S3Op::HEAD_OBJECT;
        } else if (req.method == "POST") {
            if (req.params.count("uploads")) req.op = S3Op::INIT_MULTIPART;
            else if (req.params.count("uploadId")) req.op = S3Op::COMPLETE_MULTIPART;
        }
    }

    static std::string UrlDecode(const std::string& str) {
        std::string result;
        for (size_t i = 0; i < str.size(); ++i) {
            if (str[i] == '%' && i + 2 < str.size()) {
                int hex = 0;
                if (sscanf(str.substr(i + 1, 2).c_str(), "%x", &hex) == 1) {
                    result += static_cast<char>(hex);
                    i += 2;
                } else result += str[i];
            } else if (str[i] == '+') result += ' ';
            else result += str[i];
        }
        return result;
    }
};

} // namespace s3
} // namespace nebulastore
