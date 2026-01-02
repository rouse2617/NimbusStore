// ================================
// 主程序 - NebulaStore 2.0
// ================================

#include <iostream>
#include <signal.h>
#include <memory>
#include <atomic>
#include "nebulastore/protocol/http_server.h"
#include "nebulastore/common/logger_v2.h"
#include "nebulastore/common/dout.h"

using namespace nebulastore;

namespace {

std::unique_ptr<HttpServer> g_http_server;
std::atomic<bool> g_running(true);

void SignalHandler(int signal) {
    dinfo << "收到信号 " << signal << "，正在关闭..." << dendl;
    g_running = false;
    if (g_http_server) {
        g_http_server->Stop();
    }
}

} // namespace

int main(int argc, char** argv) {
    // 初始化日志
    Logger::Instance()->Init("nebula.log");

    dinfo << "NebulaStore 2.0 - AI Training Storage System" << dendl;
    dinfo << "=============================================" << dendl;

    // 创建 HTTP 服务器
    g_http_server = std::make_unique<HttpServer>("0.0.0.0", 8080);

    // 启用 S3 API
    g_http_server->EnableS3("/tmp/nebula-s3-data");

    // 注册路由处理器

    // 健康检查端点
    g_http_server->RegisterHandler("GET", "/health", [](const std::string& method,
                                                          const std::string& path,
                                                          const std::string& body) {
        (void)method; (void)path; (void)body;
        return R"({"status":"ok","service":"nebulastore"})";
    });

    // 启动 HTTP 服务器
    if (!g_http_server->Start()) {
        derr << "HTTP 服务器启动失败" << dendl;
        return 1;
    }

    // 注册信号处理
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    dinfo << "服务已启动，按 Ctrl+C 停止..." << dendl;

    // 保持运行
    while (g_running) {
        sleep(1);
    }

    dinfo << "NebulaStore 已关闭" << dendl;
    return 0;
}
