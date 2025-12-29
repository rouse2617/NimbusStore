// ================================
// 阶段 0 最小验证测试
// ================================

#include <gtest/gtest.h>
#include <filesystem>
#include "yig/metadata/metadata_service.h"
#include "yig/metadata/rocksdb_store.h"
#include "yig/storage/backend.h"

namespace fs = std::filesystem;
namespace yig::test {

class Stage0Test : public ::testing::Test {
protected:
    void SetUp() override {
        // 清理测试目录
        test_dir_ = "/tmp/yig_test_XXXXXX";
        // 使用 mkdtemp 创建临时目录
        char* temp_dir = mkdtemp(const_cast<char*>(test_dir_.data()));
        ASSERT_NE(temp_dir, nullptr);

        meta_db_path_ = test_dir_ + "/metadata";
        data_dir_path_ = test_dir_ + "/data";

        fs::create_directories(meta_db_path_);
        fs::create_directories(data_dir_path_);

        // 初始化 RocksDB 存储
        metadata::RocksDBStore::Config meta_config;
        meta_config.db_path = meta_db_path_;
        auto store = std::make_shared<metadata::RocksDBStore>(meta_config);
        auto status = store->Init();
        ASSERT_TRUE(status.OK()) << "Failed to init RocksDB: " << status.message();

        // 初始化元数据分区
        metadata::MetaPartition::Config part_config;
        part_config.start_inode = 1;
        part_config.end_inode = 1000000;
        part_config.data_dir = meta_db_path_;

        partition_ = std::make_unique<metadata::MetaPartition>(part_config);
        status = partition_->Init();
        ASSERT_TRUE(status.OK()) << "Failed to init partition: " << status.message();

        // 初始化存储后端
        storage::LocalBackend::Config storage_config;
        storage_config.data_dir = data_dir_path_;
        storage_backend_ = std::make_shared<storage::LocalBackend>(storage_config);
    }

    void TearDown() override {
        // 清理测试目录
        if (!test_dir_.empty()) {
            fs::remove_all(test_dir_);
        }
    }

    std::string test_dir_;
    std::string meta_db_path_;
    std::string data_dir_path_;
    std::unique_ptr<metadata::MetaPartition> partition_;
    std::shared_ptr<storage::StorageBackend> storage_backend_;
};

// ================================
// 测试 1: 创建目录
// ================================

TEST_F(Stage0Test, CreateDirectory) {
    // TODO: 创建根目录 "/"
    // TODO: 创建子目录 "/data"
    // TODO: 验证目录存在
}

// ================================
// 测试 2: 创建文件
// ================================

TEST_F(Stage0Test, CreateFile) {
    // TODO: 创建文件 "/data/test.txt"
    // TODO: 验证 inode 存在
    // TODO: 验证 dentry 存在
}

// ================================
// 测试 3: 写入数据
// ================================

TEST_F(Stage0Test, WriteData) {
    // TODO: 创建文件
    // TODO: 写入数据 "Hello, World!"
    // TODO: 读取数据验证
}

// ================================
// 测试 4: 列出目录
// ================================

TEST_F(Stage0Test, ListDirectory) {
    // TODO: 创建目录 "/data"
    // TODO: 创建文件 "/data/a.txt"
    // TODO: 创建文件 "/data/b.txt"
    // TODO: 列出目录，验证包含 a.txt 和 b.txt
}

// ================================
// 测试 5: 删除文件
// ================================

TEST_F(Stage0Test, DeleteFile) {
    // TODO: 创建文件 "/data/test.txt"
    // TODO: 删除文件
    // TODO: 验证文件不存在
}

} // namespace yig::test

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
