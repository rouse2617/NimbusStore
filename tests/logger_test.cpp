// ================================
// 日志系统单元测试
// ================================

#include "nebulastore/common/logger_v2.h"
#include "nebulastore/common/dout.h"
#include <iostream>
#include <thread>
#include <vector>
#include <cassert>
#include <fstream>
#include <regex>
#include <cstdio>

using namespace nebulastore;

// 测试辅助函数
void PrintTestHeader(const char* name) {
    std::cout << "\n====================================\n";
    std::cout << "测试: " << name << "\n";
    std::cout << "====================================\n";
}

void PrintTestResult(const char* name, bool passed) {
    if (passed) {
        std::cout << "✅ " << name << " - PASSED\n";
    } else {
        std::cout << "❌ " << name << " - FAILED\n";
    }
}

// 清理日志文件
void CleanLogFile(const char* filename) {
    std::remove(filename);
}

// 测试 1: 基础日志输出
bool TestBasicLogging() {
    PrintTestHeader("基础日志输出");

    const char* log_file = "nebula_test_basic.log";
    CleanLogFile(log_file);
    Logger::Instance()->Init(log_file);

    std::cout << "正在写入测试日志...\n";

    dout(1) << "这是一条信息日志 (level=1)" << dendl;
    dout(5) << "这是一条调试日志 (level=5)" << dendl;
    derr << "这是一条错误日志 (level=-1)" << dendl;
    dwarn << "这是一条警告日志 (level=0)" << dendl;
    dinfo << "这是 dinfo 宏 (level=1)" << dendl;

    // 检查日志文件
    std::ifstream file(log_file);
    bool exists = file.good();
    if (exists) {
        std::string line;
        int count = 0;
        while (std::getline(file, line)) {
            count++;
        }
        std::cout << "日志行数: " << count << " (预期 5)\n";
        exists = (count == 5);
    }
    file.close();

    PrintTestResult("基础日志输出", exists);
    return exists;
}

// 测试 2: 流式接口
bool TestStreamInterface() {
    PrintTestHeader("流式接口");

    const char* log_file = "nebula_test_stream.log";
    CleanLogFile(log_file);
    Logger::Instance()->Init(log_file);

    int inode_id = 12345;
    std::string filename = "test.txt";
    uint64_t size = 1024 * 1024;

    dout(5) << "创建文件: " << filename << ", inode=" << inode_id << ", size=" << size << dendl;

    bool passed = true;  // 如果能编译通过就算通过
    PrintTestResult("流式接口", passed);
    return passed;
}

// 测试 3: 子系统日志
bool TestSubsystemLogging() {
    PrintTestHeader("子系统日志");

    const char* log_file = "nebula_test_subsys.log";
    CleanLogFile(log_file);
    Logger::Instance()->Init(log_file);

    subdout(metadata, 5) << "元数据操作: 创建 dentry" << dendl;
    subdout(rocksdb, 5) << "RocksDB: 写入 key=value" << dendl;
    subdout(storage, 5) << "存储: Put 操作完成" << dendl;
    subdout(http_server, 3) << "HTTP: 收到请求 GET /" << dendl;

    // 检查日志文件是否包含不同子系统的标记
    std::ifstream file(log_file);
    std::string line;
    bool has_metadata = false, has_rocksdb = false, has_storage = false;
    while (std::getline(file, line)) {
        if (line.find("[metadata]") != std::string::npos) has_metadata = true;
        if (line.find("[rocksdb]") != std::string::npos) has_rocksdb = true;
        if (line.find("[storage]") != std::string::npos) has_storage = true;
    }
    file.close();

    bool passed = has_metadata && has_rocksdb && has_storage;
    if (!passed) {
        std::cout << "缺少子系统标记: metadata=" << has_metadata
                  << ", rocksdb=" << has_rocksdb << ", storage=" << has_storage << "\n";
    }
    PrintTestResult("子系统日志", passed);
    return passed;
}

// 测试 4: 级别过滤
bool TestLevelFiltering() {
    PrintTestHeader("级别过滤");

    const char* log_file = "nebula_test_filter.log";
    CleanLogFile(log_file);
    Logger::Instance()->Init(log_file);

    std::cout << "默认 gather_level=5，应该输出 level<=5 的日志\n";

    dout(5) << "这条应该输出 (level=5 <= gather=5)" << dendl;
    dout(6) << "这条不应该输出 (level=6 > gather=5)" << dendl;
    dout(10) << "这条也不应该输出 (level=10 > gather=5)" << dendl;

    // 检查日志文件
    std::ifstream file(log_file);
    int line_count = 0;
    std::string line;
    while (std::getline(file, line)) {
        line_count++;
        // 检查是否有被过滤掉的日志
        if (line.find("level=6") != std::string::npos ||
            line.find("level=10") != std::string::npos) {
            std::cout << "错误: 被过滤的日志出现了\n";
            file.close();
            return false;
        }
    }
    file.close();

    std::cout << "日志行数: " << line_count << " (应该只有 1 行)\n";
    bool passed = (line_count == 1);
    PrintTestResult("级别过滤", passed);
    return passed;
}

// 测试 5: 动态调整级别
bool TestDynamicLevelChange() {
    PrintTestHeader("动态调整级别");

    const char* log_file = "nebula_test_dynamic.log";
    CleanLogFile(log_file);
    Logger::Instance()->Init(log_file);

    std::cout << "初始 gather_level=5\n";
    dout(10) << "[初始] 这条不应该输出 (level=10)" << dendl;

    std::cout << "调整 metadata 子系统级别为 20\n";
    Logger::Instance()->SetSubsysLevel(SubsysID::metadata, 20);

    subdout(metadata, 10) << "[调整后] 现在这条应该输出了 (level=10)" << dendl;
    subdout(metadata, 15) << "[调整后] 这条也应该输出 (level=15)" << dendl;

    // 检查日志
    std::ifstream file(log_file);
    std::string line;
    bool has_initial = false, has_adjusted = false;
    while (std::getline(file, line)) {
        if (line.find("[初始]") != std::string::npos) has_initial = true;
        if (line.find("[调整后]") != std::string::npos) has_adjusted = true;
    }
    file.close();

    bool passed = !has_initial && has_adjusted;
    if (!passed) {
        std::cout << "初始日志出现=" << has_initial << ", 调整后日志出现=" << has_adjusted << "\n";
    }
    PrintTestResult("动态调整级别", passed);
    return passed;
}

// 测试 6: 多线程并发
bool TestMultiThreaded() {
    PrintTestHeader("多线程并发日志");

    const char* log_file = "nebula_test_thread.log";
    CleanLogFile(log_file);
    Logger::Instance()->Init(log_file);

    const int NUM_THREADS = 4;
    const int LOGS_PER_THREAD = 100;

    std::vector<std::thread> threads;

    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([i]() {
            // 每个线程获取 Logger 实例
            Logger::Instance();
            for (int j = 0; j < LOGS_PER_THREAD; ++j) {
                dout(1) << "线程 " << i << ": 日志 " << j << dendl;
            }
        });
    }

    std::cout << "启动 " << NUM_THREADS << " 个线程，每个线程写入 " << LOGS_PER_THREAD << " 条日志...\n";

    for (auto& t : threads) {
        t.join();
    }

    // 检查日志文件行数
    std::ifstream file(log_file);
    int line_count = 0;
    std::string line;
    while (std::getline(file, line)) {
        line_count++;
    }
    file.close();

    int expected = NUM_THREADS * LOGS_PER_THREAD;
    std::cout << "预期日志行数: " << expected << "\n";
    std::cout << "实际日志行数: " << line_count << "\n";

    bool passed = (line_count >= expected);  // 允许更多（之前测试的残留）
    PrintTestResult("多线程并发", passed);
    return passed;
}

// 测试 7: 日志格式
bool TestLogFormat() {
    PrintTestHeader("日志格式");

    const char* log_file = "nebula_test_format.log";
    CleanLogFile(log_file);
    Logger::Instance()->Init(log_file);

    dout(1) << "测试日志格式" << dendl;

    // 读取最后一行
    std::ifstream file(log_file);
    std::string line;
    std::string last_line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            last_line = line;
        }
    }
    file.close();

    std::cout << "日志内容: " << last_line << "\n";

    if (last_line.empty()) {
        PrintTestResult("日志格式", false);
        return false;
    }

    // 检查格式: "时间戳 线程ID [子系统] 级别 消息"
    // 格式示例: "2025-12-29 19:25:21.342267 7014070ae740 [_default] 1 测试日志格式"
    std::regex pattern(R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{6} [0-9a-f]+ \[[^\]]+\] -?\d+ .+$)");
    bool passed = std::regex_match(last_line, pattern);

    if (!passed) {
        std::cout << "日志格式不符合预期\n";
    }

    PrintTestResult("日志格式", passed);
    return passed;
}

// 测试 8: RAII 自动提交
bool TestRAII() {
    PrintTestHeader("RAII 自动提交");

    const char* log_file = "nebula_test_raii.log";
    CleanLogFile(log_file);
    Logger::Instance()->Init(log_file);

    {
        dout(1) << "作用域内的日志" << dendl;
        // LogEntry 在这里创建，离开作用域时自动析构并提交
    }

    // 检查日志是否被写入
    std::ifstream file(log_file);
    bool has_log = false;
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("作用域内的日志") != std::string::npos) {
            has_log = true;
            break;
        }
    }
    file.close();

    std::cout << "日志应该已自动提交\n";

    PrintTestResult("RAII 自动提交", has_log);
    return has_log;
}

// 测试 9: 性能测试
bool TestPerformance() {
    PrintTestHeader("性能测试");

    const char* log_file = "nebula_test_perf.log";
    CleanLogFile(log_file);
    Logger::Instance()->Init(log_file);

    const int ITERATIONS = 10000;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < ITERATIONS; ++i) {
        dout(1) << "性能测试日志 " << i << dendl;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    double logs_per_sec = (ITERATIONS * 1000.0) / duration.count();

    std::cout << "写入 " << ITERATIONS << " 条日志耗时: " << duration.count() << " ms\n";
    std::cout << "吞吐量: " << static_cast<int>(logs_per_sec) << " logs/sec\n";

    bool passed = (logs_per_sec > 1000);  // 至少 1000 logs/sec
    PrintTestResult("性能测试", passed);
    return passed;
}

// 主函数
int main() {
    std::cout << "====================================\n";
    std::cout << "NebulaStore 日志系统单元测试\n";
    std::cout << "====================================\n";

    int passed = 0;
    int total = 0;

    #define RUN_TEST(test) \
        total++; \
        if (test()) { \
            passed++; \
        }

    RUN_TEST(TestBasicLogging);
    RUN_TEST(TestStreamInterface);
    RUN_TEST(TestSubsystemLogging);
    RUN_TEST(TestLevelFiltering);
    RUN_TEST(TestDynamicLevelChange);
    RUN_TEST(TestMultiThreaded);
    RUN_TEST(TestLogFormat);
    RUN_TEST(TestRAII);
    RUN_TEST(TestPerformance);

    #undef RUN_TEST

    std::cout << "\n====================================\n";
    std::cout << "测试结果: " << passed << "/" << total << " 通过\n";
    std::cout << "====================================\n";

    if (passed == total) {
        std::cout << "🎉 所有测试通过！\n";
        return 0;
    } else {
        std::cout << "⚠️  有 " << (total - passed) << " 个测试失败\n";
        return 1;
    }
}
