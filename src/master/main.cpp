// ================================
// 主程序骨架 - 等你来完善
// ================================

#include <iostream>
#include <signal.h>
#include "nebulastore/metadata/metadata_service.h"
#include "nebulastore/metadata/rocksdb_store.h"
#include "nebulastore/storage/backend.h"
#include "nebulastore/protocol/gateway.h"
#include "nebulastore/common/logger.h"

using namespace nebulastore;

namespace {

std::unique_ptr<protocol::S3Gateway> g_s3_gateway;
std::unique_ptr<protocol::FuseClient> g_fuse_client;

void SignalHandler(int signal) {
    std::cout << "\nShutting down...\n";

    if (g_s3_gateway) {
        g_s3_gateway->Stop();
    }
    if (g_fuse_client) {
        g_fuse_client->Unmount();
    }

    exit(0);
}

} // namespace

int main(int argc, char** argv) {
    // TODO: 解析命令行参数
    // TODO: 加载配置文件

    // 初始化日志
    Logger::Instance()->Init("/var/log/yig/yig.log", "info");

    std::cout << "YIG 2.0 - AI Training Storage System\n";
    std::cout << "=====================================\n\n";

    // 初始化元数据存储
    auto metadata_store = std::make_shared<metadata::RocksDBStore>(
        metadata::RocksDBStore::Config{
            .db_path = "/var/lib/yig/metadata",
            .create_if_missing = true,
        }
    );
    auto status = metadata_store->Init();
    if (!status.OK()) {
        std::cerr << "Failed to initialize metadata store: " << status.message() << "\n";
        return 1;
    }

    // 初始化元数据分区
    auto partition = std::make_unique<metadata::MetaPartition>(
        metadata::MetaPartition::Config{
            .start_inode = 1,
            .end_inode = 1e12,  // 1 万亿
            .data_dir = "/var/lib/yig/metadata",
        }
    );
    status = partition->Init();
    if (!status.OK()) {
        std::cerr << "Failed to initialize partition: " << status.message() << "\n";
        return 1;
    }

    // 初始化元数据服务
    metadata::MetadataServiceImpl::Config meta_config;
    meta_config.partitions.push_back(std::move(partition));

    auto metadata_service = std::make_shared<metadata::MetadataServiceImpl>(
        std::move(meta_config)
    );

    // 初始化存储后端
    auto storage_backend = std::make_shared<storage::LocalBackend>(
        storage::LocalBackend::Config{
            .data_dir = "/var/lib/yig/data",
        }
    );

    // 初始化命名空间服务
    auto namespace_service = std::make_shared<namespace_::NamespaceService>(
        namespace_::NamespaceService::Config{
            .metadata_service = metadata_service,
            .storage_backend = storage_backend,
            .default_bucket = "default",
        }
    );

    // 初始化 S3 Gateway
    protocol::S3Gateway::Config s3_config;
    s3_config.namespace_service = namespace_service;
    s3_config.host = "0.0.0.0";
    s3_config.port = 8080;

    g_s3_gateway = std::make_unique<protocol::S3Gateway>(s3_config);
    status = g_s3_gateway->Start();
    if (!status.OK()) {
        std::cerr << "Failed to start S3 Gateway: " << status.message() << "\n";
        return 1;
    }

    std::cout << "S3 Gateway started on http://0.0.0.0:8080\n";
    std::cout << "Press Ctrl+C to stop...\n";

    // 注册信号处理
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    // 等待退出
    g_s3_gateway->Join();

    return 0;
}
