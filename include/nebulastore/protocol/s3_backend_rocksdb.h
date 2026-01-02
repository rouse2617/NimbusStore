// RocksDB 元数据后端实现
#pragma once

#include "nebulastore/protocol/s3_metadata.h"
#include <rocksdb/db.h>
#include <rocksdb/write_batch.h>
#include <stdexcept>

namespace nebulastore {
namespace s3 {

class RocksDBBackend : public MetadataBackend {
public:
    explicit RocksDBBackend(const std::string& db_path) {
        rocksdb::Options options;
        options.create_if_missing = true;
        rocksdb::Status status = rocksdb::DB::Open(options, db_path, &db_);
        if (!status.ok()) {
            throw std::runtime_error("Failed to open RocksDB: " + status.ToString());
        }
    }

    ~RocksDBBackend() override { delete db_; }

    bool Put(const std::string& key, const std::string& value) override {
        return db_->Put(rocksdb::WriteOptions(), key, value).ok();
    }

    bool Get(const std::string& key, std::string& value) override {
        return db_->Get(rocksdb::ReadOptions(), key, &value).ok();
    }

    bool Delete(const std::string& key) override {
        return db_->Delete(rocksdb::WriteOptions(), key).ok();
    }

    bool Exists(const std::string& key) override {
        std::string value;
        return db_->Get(rocksdb::ReadOptions(), key, &value).ok();
    }

    bool BatchPut(const std::vector<std::pair<std::string, std::string>>& kvs) override {
        rocksdb::WriteBatch batch;
        for (const auto& [k, v] : kvs) {
            batch.Put(k, v);
        }
        return db_->Write(rocksdb::WriteOptions(), &batch).ok();
    }

    std::vector<std::pair<std::string, std::string>> Scan(const std::string& prefix, int limit) override {
        std::vector<std::pair<std::string, std::string>> result;
        std::unique_ptr<rocksdb::Iterator> it(db_->NewIterator(rocksdb::ReadOptions()));

        for (it->Seek(prefix); it->Valid() && it->key().starts_with(prefix); it->Next()) {
            result.emplace_back(it->key().ToString(), it->value().ToString());
            if ((int)result.size() >= limit) break;
        }
        return result;
    }

private:
    rocksdb::DB* db_ = nullptr;
};

// 注册 RocksDB 后端
inline void RegisterRocksDBBackend() {
    MetadataBackendFactory::Instance().Register("rocksdb",
        [](const std::string& path) -> std::unique_ptr<MetadataBackend> {
            return std::make_unique<RocksDBBackend>(path);
        });
}

} // namespace s3
} // namespace nebulastore
