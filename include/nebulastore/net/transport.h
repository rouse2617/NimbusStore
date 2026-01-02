#pragma once

#include <memory>
#include <string>
#include <functional>
#include <cstdint>
#include "nebulastore/common/types.h"
#include "nebulastore/common/async.h"

namespace nebulastore::net {

// ================================
// Transport 配置
// ================================

struct TransportConfig {
    std::string host;
    uint16_t port = 0;
    uint32_t timeout_ms = 5000;
    uint32_t max_retries = 3;
    size_t send_buffer_size = 64 * 1024;
    size_t recv_buffer_size = 64 * 1024;
    bool use_rdma = false;
};

// ================================
// RDMA 内存区域描述
// ================================

struct RDMAMemoryRegion {
    void* addr = nullptr;
    size_t length = 0;
    uint32_t lkey = 0;
    uint32_t rkey = 0;
};

// ================================
// Transport 基类
// ================================

class Transport {
public:
    virtual ~Transport() = default;

    // 连接管理
    virtual AsyncTask<Status> Connect(const std::string& host, uint16_t port) = 0;
    virtual void Close() = 0;
    virtual bool IsConnected() const = 0;

    // 数据传输
    virtual AsyncTask<Status> Send(const void* data, size_t size) = 0;
    virtual AsyncTask<Status> Recv(void* buffer, size_t size, size_t* bytes_read) = 0;

    // RDMA 能力查询
    virtual bool SupportsRDMA() const { return false; }
    virtual AsyncTask<Status> RDMARead(const RDMAMemoryRegion& remote,
                                       const RDMAMemoryRegion& local) {
        co_return Status::InvalidArgument("RDMA not supported");
    }
    virtual AsyncTask<Status> RDMAWrite(const RDMAMemoryRegion& local,
                                        const RDMAMemoryRegion& remote) {
        co_return Status::InvalidArgument("RDMA not supported");
    }
};

// ================================
// TransportListener 监听器接口
// ================================

class TransportListener {
public:
    virtual ~TransportListener() = default;

    virtual AsyncTask<Status> Bind(const std::string& host, uint16_t port) = 0;
    virtual AsyncTask<std::unique_ptr<Transport>> Accept() = 0;
    virtual void Close() = 0;
};

// ================================
// TCPTransport 实现
// ================================

class TCPTransport : public Transport {
public:
    TCPTransport();
    explicit TCPTransport(int fd);
    ~TCPTransport() override;

    AsyncTask<Status> Connect(const std::string& host, uint16_t port) override;
    void Close() override;
    bool IsConnected() const override;

    AsyncTask<Status> Send(const void* data, size_t size) override;
    AsyncTask<Status> Recv(void* buffer, size_t size, size_t* bytes_read) override;

private:
    int fd_ = -1;
};

// ================================
// TCPListener 实现
// ================================

class TCPListener : public TransportListener {
public:
    TCPListener();
    ~TCPListener() override;

    AsyncTask<Status> Bind(const std::string& host, uint16_t port) override;
    AsyncTask<std::unique_ptr<Transport>> Accept() override;
    void Close() override;

private:
    int listen_fd_ = -1;
};

// ================================
// RDMATransport 接口预留
// ================================

class RDMATransport : public Transport {
public:
    ~RDMATransport() override = default;

    bool SupportsRDMA() const override { return true; }

    // RDMA 特有操作
    virtual AsyncTask<Status> RegisterMemory(void* addr, size_t size,
                                             RDMAMemoryRegion* region) = 0;
    virtual AsyncTask<Status> DeregisterMemory(const RDMAMemoryRegion& region) = 0;
};

// ================================
// 工厂函数
// ================================

std::unique_ptr<Transport> CreateTransport(const TransportConfig& config);
std::unique_ptr<TransportListener> CreateListener(const TransportConfig& config);

} // namespace nebulastore::net
