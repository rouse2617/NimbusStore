#pragma once

#include "nebulastore/common/subsys.h"
#include <string>
#include <array>
#include <mutex>
#include <fstream>

namespace nebulastore {

// ================================
// Logger - 日志器 (新版本)
// ================================
class Logger {
public:
    static Logger* Instance();

    // 初始化日志系统
    void Init(const std::string& log_file = "");

    // 检查是否应该收集某个级别的日志
    bool ShouldGather(SubsysID subsys, int level) const;

    // 写入日志 (由 LogEntry 析构函数调用)
    void WriteLog(SubsysID subsys, int level, const std::string& message);

    // 设置子系统的日志级别
    void SetSubsysLevel(SubsysID subsys, uint8_t level);

    // 获取子系统的收集级别
    uint8_t GetSubsysLevel(SubsysID subsys) const;

private:
    Logger() = default;
    ~Logger() = default;

    // 禁止拷贝
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // 子系统日志级别配置
    std::array<uint8_t, kSubsysCount> subsys_levels_ = {};

    // 默认日志级别
    uint8_t default_level_ = 5;

    // 日志文件
    std::ofstream log_file_;

    // 互斥锁 (保护文件写入)
    mutable std::mutex mutex_;

    // 是否使用文件输出
    bool use_file_ = false;

    // 获取当前时间戳字符串
    // 格式: "2025-01-15 10:30:45.123456"
    std::string GetTimestamp() const;

    // 获取子系统名称
    const char* GetSubsysName(SubsysID subsys) const;
};

} // namespace nebulastore
