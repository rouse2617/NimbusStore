#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "nebulastore/storage/backend.h"

namespace nebulastore::storage {

// 通用配置结构
struct Config {
    std::string type;           // local/s3/minio
    std::string data_dir;       // local backend
    std::string endpoint;       // s3/minio
    std::string access_key;
    std::string secret_key;
    std::string region;
    std::string bucket;
};

// 后端创建器类型
using BackendCreator = std::function<std::unique_ptr<StorageBackend>(const Config&)>;

// 存储后端工厂 (单例模式)
class BackendFactory {
public:
    static BackendFactory& Instance() {
        static BackendFactory instance;
        return instance;
    }

    // 注册后端
    void Register(const std::string& name, BackendCreator creator) {
        creators_[name] = std::move(creator);
    }

    // 创建后端实例
    std::unique_ptr<StorageBackend> Create(const std::string& name, const Config& config) {
        auto it = creators_.find(name);
        if (it == creators_.end()) {
            return nullptr;
        }
        return it->second(config);
    }

    // 返回已注册的后端列表
    std::vector<std::string> Drivers() const {
        std::vector<std::string> names;
        names.reserve(creators_.size());
        for (const auto& [name, _] : creators_) {
            names.push_back(name);
        }
        return names;
    }

private:
    BackendFactory() = default;
    std::unordered_map<std::string, BackendCreator> creators_;
};

// 自动注册辅助类
class BackendRegistrar {
public:
    BackendRegistrar(const std::string& name, BackendCreator creator) {
        BackendFactory::Instance().Register(name, std::move(creator));
    }
};

// 注册宏
#define REGISTER_BACKEND(name, creator) \
    static BackendRegistrar g_##name##_registrar(#name, creator)

// 内置后端注册
inline void RegisterBuiltinBackends() {
    // local 后端
    BackendFactory::Instance().Register("local", [](const Config& cfg) {
        LocalBackend::Config local_cfg{cfg.data_dir};
        return std::make_unique<LocalBackend>(local_cfg);
    });

    // s3 后端
    BackendFactory::Instance().Register("s3", [](const Config& cfg) {
        S3Backend::Config s3_cfg{
            cfg.access_key, cfg.secret_key, cfg.region,
            cfg.endpoint, cfg.bucket
        };
        return std::make_unique<S3Backend>(s3_cfg);
    });

    // minio 后端 (使用 S3 兼容接口)
    BackendFactory::Instance().Register("minio", [](const Config& cfg) {
        S3Backend::Config minio_cfg{
            cfg.access_key, cfg.secret_key, cfg.region,
            cfg.endpoint, cfg.bucket
        };
        return std::make_unique<S3Backend>(minio_cfg);
    });
}

} // namespace nebulastore::storage
