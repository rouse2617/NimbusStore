// ================================
// S3 存储后端实现 (基于 libcurl)
// ================================

#include "nebulastore/storage/backend.h"
#include "nebulastore/common/logger.h"
#include <curl/curl.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <algorithm>

namespace nebulastore::storage {

// ================================
// S3 签名和 HTTP 辅助类
// ================================

class S3Backend::S3Client {
public:
    explicit S3Client(const Config& config) : config_(config) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~S3Client() {
        curl_global_cleanup();
    }

    // PUT 对象
    Status PutObject(const std::string& key, const ByteBuffer& data) {
        auto url = BuildUrl(key);
        auto headers = BuildHeaders("PUT", key, data.size());

        CURL* curl = curl_easy_init();
        if (!curl) return Status::IO("Failed to init curl");

        struct curl_slist* header_list = nullptr;
        for (const auto& h : headers) {
            header_list = curl_slist_append(header_list, h.c_str());
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(curl, CURLOPT_READDATA, &data);
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, ReadCallback);
        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(data.size()));
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_slist_free_all(header_list);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            return Status::IO(std::string("curl error: ") + curl_easy_strerror(res));
        }
        if (http_code >= 400) {
            return Status::IO("S3 PUT failed, HTTP " + std::to_string(http_code));
        }
        return Status::Ok();
    }

    // GET 对象
    Status GetObject(const std::string& key, ByteBuffer* data) {
        auto url = BuildUrl(key);
        auto headers = BuildHeaders("GET", key, 0);

        CURL* curl = curl_easy_init();
        if (!curl) return Status::IO("Failed to init curl");

        struct curl_slist* header_list = nullptr;
        for (const auto& h : headers) {
            header_list = curl_slist_append(header_list, h.c_str());
        }

        std::vector<uint8_t> buffer;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_slist_free_all(header_list);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            return Status::IO(std::string("curl error: ") + curl_easy_strerror(res));
        }
        if (http_code == 404) {
            return Status::NotFound("Object not found: " + key);
        }
        if (http_code >= 400) {
            return Status::IO("S3 GET failed, HTTP " + std::to_string(http_code));
        }

        if (data) {
            data->assign(std::move(buffer));
        }
        return Status::Ok();
    }

    // GET 范围
    Status GetObjectRange(const std::string& key, uint64_t offset, uint64_t size, ByteBuffer* data) {
        auto url = BuildUrl(key);
        auto headers = BuildHeaders("GET", key, 0);
        headers.push_back("Range: bytes=" + std::to_string(offset) + "-" + std::to_string(offset + size - 1));

        CURL* curl = curl_easy_init();
        if (!curl) return Status::IO("Failed to init curl");

        struct curl_slist* header_list = nullptr;
        for (const auto& h : headers) {
            header_list = curl_slist_append(header_list, h.c_str());
        }

        std::vector<uint8_t> buffer;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_slist_free_all(header_list);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            return Status::IO(std::string("curl error: ") + curl_easy_strerror(res));
        }
        if (http_code == 404) {
            return Status::NotFound("Object not found: " + key);
        }
        if (http_code >= 400 && http_code != 206) {
            return Status::IO("S3 GET range failed, HTTP " + std::to_string(http_code));
        }

        if (data) {
            data->assign(std::move(buffer));
        }
        return Status::Ok();
    }

    // DELETE 对象
    Status DeleteObject(const std::string& key) {
        auto url = BuildUrl(key);
        auto headers = BuildHeaders("DELETE", key, 0);

        CURL* curl = curl_easy_init();
        if (!curl) return Status::IO("Failed to init curl");

        struct curl_slist* header_list = nullptr;
        for (const auto& h : headers) {
            header_list = curl_slist_append(header_list, h.c_str());
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_slist_free_all(header_list);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            return Status::IO(std::string("curl error: ") + curl_easy_strerror(res));
        }
        if (http_code >= 400 && http_code != 404) {
            return Status::IO("S3 DELETE failed, HTTP " + std::to_string(http_code));
        }
        return Status::Ok();
    }

    // HEAD 对象 (检查存在)
    Status HeadObject(const std::string& key) {
        auto url = BuildUrl(key);
        auto headers = BuildHeaders("HEAD", key, 0);

        CURL* curl = curl_easy_init();
        if (!curl) return Status::IO("Failed to init curl");

        struct curl_slist* header_list = nullptr;
        for (const auto& h : headers) {
            header_list = curl_slist_append(header_list, h.c_str());
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);

        CURLcode res = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_slist_free_all(header_list);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            return Status::IO(std::string("curl error: ") + curl_easy_strerror(res));
        }
        if (http_code == 404) {
            return Status::NotFound("Object not found");
        }
        if (http_code >= 400) {
            return Status::IO("S3 HEAD failed, HTTP " + std::to_string(http_code));
        }
        return Status::Ok();
    }

private:
    Config config_;

    std::string BuildUrl(const std::string& key) {
        std::string host = config_.endpoint.empty()
            ? config_.bucket + ".s3." + config_.region + ".amazonaws.com"
            : config_.endpoint;
        return "https://" + host + "/" + UrlEncode(key);
    }

    std::vector<std::string> BuildHeaders(const std::string& method, const std::string& key, size_t content_length) {
        std::string date = GetAmzDate();
        std::string date_stamp = date.substr(0, 8);

        std::string host = config_.endpoint.empty()
            ? config_.bucket + ".s3." + config_.region + ".amazonaws.com"
            : config_.endpoint;

        // AWS Signature V4
        std::string canonical_uri = "/" + key;
        std::string canonical_querystring;
        std::string payload_hash = "UNSIGNED-PAYLOAD";

        std::string canonical_headers = "host:" + host + "\n" + "x-amz-content-sha256:" + payload_hash + "\n" + "x-amz-date:" + date + "\n";
        std::string signed_headers = "host;x-amz-content-sha256;x-amz-date";

        std::string canonical_request = method + "\n" + canonical_uri + "\n" + canonical_querystring + "\n" + canonical_headers + "\n" + signed_headers + "\n" + payload_hash;

        std::string algorithm = "AWS4-HMAC-SHA256";
        std::string credential_scope = date_stamp + "/" + config_.region + "/s3/aws4_request";
        std::string string_to_sign = algorithm + "\n" + date + "\n" + credential_scope + "\n" + Sha256Hex(canonical_request);

        auto signing_key = GetSignatureKey(config_.secret_key, date_stamp, config_.region, "s3");
        std::string signature = HmacSha256Hex(signing_key, string_to_sign);

        std::string authorization = algorithm + " Credential=" + config_.access_key + "/" + credential_scope + ", SignedHeaders=" + signed_headers + ", Signature=" + signature;

        return {
            "Host: " + host,
            "x-amz-date: " + date,
            "x-amz-content-sha256: " + payload_hash,
            "Authorization: " + authorization
        };
    }

    static std::string GetAmzDate() {
        time_t now = time(nullptr);
        struct tm tm_buf;
        gmtime_r(&now, &tm_buf);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y%m%dT%H%M%SZ", &tm_buf);
        return buf;
    }

    static std::string Sha256Hex(const std::string& data) {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(data.c_str()), data.size(), hash);
        return ToHex(hash, SHA256_DIGEST_LENGTH);
    }

    static std::vector<unsigned char> HmacSha256(const std::vector<unsigned char>& key, const std::string& data) {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        unsigned int len = SHA256_DIGEST_LENGTH;
        HMAC(EVP_sha256(), key.data(), key.size(),
             reinterpret_cast<const unsigned char*>(data.c_str()), data.size(), hash, &len);
        return std::vector<unsigned char>(hash, hash + len);
    }

    static std::vector<unsigned char> HmacSha256(const std::string& key, const std::string& data) {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        unsigned int len = SHA256_DIGEST_LENGTH;
        HMAC(EVP_sha256(), key.c_str(), key.size(),
             reinterpret_cast<const unsigned char*>(data.c_str()), data.size(), hash, &len);
        return std::vector<unsigned char>(hash, hash + len);
    }

    static std::string HmacSha256Hex(const std::vector<unsigned char>& key, const std::string& data) {
        auto hash = HmacSha256(key, data);
        return ToHex(hash.data(), hash.size());
    }

    static std::vector<unsigned char> GetSignatureKey(const std::string& secret, const std::string& date,
                                                       const std::string& region, const std::string& service) {
        auto k_date = HmacSha256("AWS4" + secret, date);
        auto k_region = HmacSha256(k_date, region);
        auto k_service = HmacSha256(k_region, service);
        return HmacSha256(k_service, "aws4_request");
    }

    static std::string ToHex(const unsigned char* data, size_t len) {
        std::ostringstream ss;
        ss << std::hex << std::setfill('0');
        for (size_t i = 0; i < len; ++i) {
            ss << std::setw(2) << static_cast<int>(data[i]);
        }
        return ss.str();
    }

    static std::string UrlEncode(const std::string& s) {
        std::ostringstream escaped;
        escaped << std::hex << std::setfill('0');
        for (char c : s) {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
                escaped << c;
            } else {
                escaped << '%' << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
            }
        }
        return escaped.str();
    }

    // CURL 回调
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        auto* buffer = static_cast<std::vector<uint8_t>*>(userp);
        auto* ptr = static_cast<uint8_t*>(contents);
        buffer->insert(buffer->end(), ptr, ptr + size * nmemb);
        return size * nmemb;
    }

    struct ReadContext {
        const ByteBuffer* data;
        size_t offset;
    };

    static size_t ReadCallback(void* ptr, size_t size, size_t nmemb, void* userp) {
        auto* data = static_cast<const ByteBuffer*>(userp);
        static thread_local size_t offset = 0;
        size_t remaining = data->size() - offset;
        size_t to_copy = std::min(remaining, size * nmemb);
        if (to_copy > 0) {
            memcpy(ptr, data->data() + offset, to_copy);
            offset += to_copy;
        }
        if (offset >= data->size()) {
            offset = 0;  // Reset for next request
        }
        return to_copy;
    }
};

// ================================
// S3Backend 实现
// ================================

S3Backend::S3Backend(Config config)
    : config_(std::move(config)) {
    client_ = std::make_unique<S3Client>(config_);
    LOG_INFO("S3Backend initialized: bucket=%s, region=%s",
             config_.bucket.c_str(), config_.region.c_str());
}

S3Backend::~S3Backend() = default;

AsyncTask<Status> S3Backend::Put(const std::string& key, const ByteBuffer& data) {
    auto status = client_->PutObject(key, data);
    if (status.OK()) {
        LOG_DEBUG("S3 PUT: %s (%zu bytes)", key.c_str(), data.size());
    } else {
        LOG_ERROR("S3 PUT failed: %s - %s", key.c_str(), status.message().c_str());
    }
    co_return status;
}

AsyncTask<Status> S3Backend::Get(const std::string& key, ByteBuffer* data) {
    auto status = client_->GetObject(key, data);
    if (status.OK()) {
        LOG_DEBUG("S3 GET: %s", key.c_str());
    } else {
        LOG_ERROR("S3 GET failed: %s - %s", key.c_str(), status.message().c_str());
    }
    co_return status;
}

AsyncTask<Status> S3Backend::Delete(const std::string& key) {
    auto status = client_->DeleteObject(key);
    if (status.OK()) {
        LOG_DEBUG("S3 DELETE: %s", key.c_str());
    }
    co_return status;
}

AsyncTask<Status> S3Backend::Exists(const std::string& key) {
    co_return client_->HeadObject(key);
}

AsyncTask<Status> S3Backend::GetRange(const std::string& key, uint64_t offset, uint64_t size, ByteBuffer* data) {
    auto status = client_->GetObjectRange(key, offset, size, data);
    if (status.OK()) {
        LOG_DEBUG("S3 GET range: %s [%lu-%lu]", key.c_str(), offset, offset + size);
    }
    co_return status;
}

AsyncTask<Status> S3Backend::BatchGet(const std::vector<std::string>& keys, std::vector<ByteBuffer>* data) {
    data->resize(keys.size());
    for (size_t i = 0; i < keys.size(); ++i) {
        auto status = co_await Get(keys[i], &(*data)[i]);
        if (!status.OK()) {
            co_return status;
        }
    }
    co_return Status::Ok();
}

AsyncTask<Status> S3Backend::HealthCheck() {
    // 尝试 HEAD bucket 或简单的 list 操作
    // 这里简化为检查配置是否有效
    if (config_.bucket.empty() || config_.access_key.empty()) {
        co_return Status::InvalidArgument("Invalid S3 configuration");
    }
    co_return Status::Ok();
}

AsyncTask<Status> S3Backend::GetCapacity(CapacityInfo* info) {
    // S3 存储容量理论上无限
    info->total_bytes = UINT64_MAX;
    info->used_bytes = 0;
    info->available_bytes = UINT64_MAX;
    co_return Status::Ok();
}

} // namespace nebulastore::storage
