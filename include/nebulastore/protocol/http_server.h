#pragma once

#include <string>
#include <functional>
#include <memory>
#include <cstdlib>

// 前置声明 mongoose 类型 (C 库，在全局命名空间)
struct mg_mgr;
struct mg_connection;

namespace nebulastore {

// ================================
// HTTP 请求处理器类型
// ================================
using HttpHandler = std::function<std::string(const std::string& method,
                                               const std::string& path,
                                               const std::string& body)>;

// ================================
// HttpServer - HTTP 服务器 (基于 mongoose)
// ================================
class HttpServer {
public:
    HttpServer(const std::string& address, int port);
    ~HttpServer();

    // 禁止拷贝
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // 启动 HTTP 服务器
    bool Start();

    // 停止 HTTP 服务器
    void Stop();

    // 注册路由处理器
    void RegisterHandler(const std::string& method, const std::string& path, HttpHandler handler);

    // 启用 S3 API
    void EnableS3(const std::string& data_dir);

    // 检查是否正在运行
    bool IsRunning() const { return running_; }

private:
    // 前置声明实现类
    class Impl;

    std::string address_;
    int port_;
    bool running_;
    Impl* impl_;

    // 事件处理回调 (签名必须匹配 mg_event_handler_t: void(*)(mg_connection*, int, void*))
    static void EventHandler(mg_connection* nc, int ev, void* ev_data);
};

} // namespace nebulastore
