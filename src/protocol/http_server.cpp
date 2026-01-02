// ================================
// HTTP 服务器实现
// ================================

#include "nebulastore/protocol/http_server.h"
#include "nebulastore/protocol/s3_handler.h"
#include "nebulastore/common/logger_v2.h"
#include "nebulastore/common/dout.h"
#include "mongoose.h"
#include <cstring>
#include <unordered_map>
#include <sstream>
#include <thread>
#include <atomic>

namespace nebulastore {

// ================================
// 路由表
// ================================
struct RouteKey {
    std::string method;
    std::string path;

    bool operator==(const RouteKey& other) const {
        return method == other.method && path == other.path;
    }
};

struct RouteKeyHash {
    size_t operator()(const RouteKey& k) const {
        return std::hash<std::string>()(k.method) ^ std::hash<std::string>()(k.path);
    }
};

static std::unordered_map<RouteKey, HttpHandler, RouteKeyHash> g_routes;
static std::unique_ptr<s3::S3Handler> g_s3_handler;

// ================================
// HttpServer 实现
// ================================
class HttpServer::Impl {
public:
    mg_mgr mgr_{};
    std::thread poll_thread_;
    std::atomic<bool> running_{false};
};

HttpServer::HttpServer(const std::string& address, int port)
    : address_(address), port_(port), running_(false), impl_(nullptr) {
}

HttpServer::~HttpServer() {
    Stop();
}

bool HttpServer::Start() {
    if (running_) {
        dwarn << "HTTP 服务器已在运行" << dendl;
        return true;
    }

    // 创建实现对象
    impl_ = new Impl();
    impl_->running_ = true;

    // 初始化 mongoose 事件管理器
    mg_mgr_init(&impl_->mgr_);

    // 构造监听地址
    std::ostringstream addr;
    addr << address_ << ":" << port_;
    std::string listen_addr = addr.str();

    // 创建 HTTP 监听器
    struct mg_connection* nc = mg_http_listen(&impl_->mgr_, listen_addr.c_str(),
                                              EventHandler, this);
    if (!nc) {
        derr << "HTTP 服务器启动失败，无法监听: " << listen_addr << dendl;
        mg_mgr_free(&impl_->mgr_);
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    running_ = true;
    dinfo << "HTTP 服务器启动成功，监听: " << listen_addr << dendl;

    // 启动事件循环线程
    impl_->poll_thread_ = std::thread([this]() {
        while (impl_->running_) {
            mg_mgr_poll(&impl_->mgr_, 1000);  // 1秒超时
        }
    });

    return true;
}

void HttpServer::Stop() {
    if (!running_) {
        return;
    }

    dinfo << "正在关闭 HTTP 服务器..." << dendl;
    running_ = false;

    if (impl_) {
        // 停止事件循环
        impl_->running_ = false;

        // 等待线程结束
        if (impl_->poll_thread_.joinable()) {
            impl_->poll_thread_.join();
        }

        // 释放资源
        mg_mgr_free(&impl_->mgr_);
        delete impl_;
        impl_ = nullptr;
    }

    dinfo << "HTTP 服务器已关闭" << dendl;
}

void HttpServer::RegisterHandler(const std::string& method, const std::string& path, HttpHandler handler) {
    RouteKey key{method, path};
    g_routes[key] = handler;
    dout(5) << "注册路由: " << method << " " << path << dendl;
}

void HttpServer::EnableS3(const std::string& data_dir) {
    g_s3_handler = std::make_unique<s3::S3Handler>(nullptr, data_dir);
    dinfo << "S3 API 已启用，数据目录: " << data_dir << dendl;
}

// ================================
// 事件处理回调 (签名必须匹配 mg_event_handler_t)
// ================================
void HttpServer::EventHandler(mg_connection* nc, int ev, void* ev_data) {
    switch (ev) {
    case MG_EV_HTTP_MSG: {
        struct mg_http_message* hm = static_cast<struct mg_http_message*>(ev_data);

        // 提取方法和路径 (mongoose 使用 buf 而非 ptr)
        std::string method(hm->method.buf, hm->method.len);
        std::string path(hm->uri.buf, hm->uri.len);

        // 记录访问日志
        dout(3) << "HTTP 请求: " << method << " " << path << dendl;

        // 查找精确路由处理器
        RouteKey key{method, path};
        auto it = g_routes.find(key);

        std::string response;
        std::string content_type = "application/json";
        std::string extra_headers;
        int status_code = 200;

        if (it != g_routes.end()) {
            // 精确匹配路由
            std::string body(hm->body.buf, hm->body.len);
            response = it->second(method, path, body);
        } else if (g_s3_handler) {
            // S3 API 处理
            s3::S3Request s3_req;
            s3_req.method = method;
            s3_req.uri = path;
            s3_req.body = std::string(hm->body.buf, hm->body.len);

            // 提取 HTTP 头
            for (int i = 0; i < MG_MAX_HTTP_HEADERS && hm->headers[i].name.len > 0; i++) {
                std::string name(hm->headers[i].name.buf, hm->headers[i].name.len);
                std::string value(hm->headers[i].value.buf, hm->headers[i].value.len);
                s3_req.headers[name] = value;
            }

            s3::S3Response s3_resp = g_s3_handler->Handle(s3_req);
            status_code = s3_resp.status_code;
            response = s3_resp.body;
            content_type = s3_resp.content_type;

            // 添加 S3 响应头
            for (const auto& [key, value] : s3_resp.headers) {
                extra_headers += key + ": " + value + "\r\n";
            }
        } else {
            // 未找到路由
            response = R"({"error": "Not Found", "path": ")" + path + R"("})";
            status_code = 404;
        }

        // 发送响应
        std::string headers = "Content-Type: " + content_type + "\r\n" + extra_headers;
        mg_http_reply(nc, status_code, headers.c_str(), "%s", response.c_str());
        break;
    }
    default:
        break;
    }
}

} // namespace nebulastore
