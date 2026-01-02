#pragma once

#include <iostream>
#include <fstream>
#include <mutex>
#include <string>
#include <sstream>
#include <cstdio>
#include "nebulastore/common/subsys.h"

namespace nebulastore {

// ================================
// 简单日志系统 (无外部依赖)
// ================================

class Logger {
public:
    static Logger* Instance();

    void Init(const std::string& log_file, const std::string& level = "info");
    void Init(const std::string& log_file);  // 单参数版本

    // dout.h 需要的方法
    bool ShouldGather(SubsysID subsys, int level) const;
    void WriteLog(SubsysID subsys, int level, const std::string& msg);

    void Log(const std::string& level, const std::string& msg);

    void Info(const std::string& msg);
    void Warn(const std::string& msg);
    void Error(const std::string& msg);
    void Debug(const std::string& msg);

private:
    Logger() = default;
    std::ofstream file_stream_;
    mutable std::mutex mutex_;
    bool use_file_ = false;
    int log_level_ = 5;  // 默认收集级别
};

// 格式化日志宏 (支持 printf 风格)
#define LOG_INFO(fmt, ...) do { \
    char _buf[1024]; \
    snprintf(_buf, sizeof(_buf), fmt, ##__VA_ARGS__); \
    Logger::Instance()->Info(_buf); \
} while(0)

#define LOG_WARN(fmt, ...) do { \
    char _buf[1024]; \
    snprintf(_buf, sizeof(_buf), fmt, ##__VA_ARGS__); \
    Logger::Instance()->Warn(_buf); \
} while(0)

#define LOG_ERROR(fmt, ...) do { \
    char _buf[1024]; \
    snprintf(_buf, sizeof(_buf), fmt, ##__VA_ARGS__); \
    Logger::Instance()->Error(_buf); \
} while(0)

#define LOG_DEBUG(fmt, ...) do { \
    char _buf[1024]; \
    snprintf(_buf, sizeof(_buf), fmt, ##__VA_ARGS__); \
    Logger::Instance()->Debug(_buf); \
} while(0)

} // namespace nebulastore
