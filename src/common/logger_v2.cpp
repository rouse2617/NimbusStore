// ================================
// Logger 实现 (新版本)
// ================================

#include "nebulastore/common/logger_v2.h"
#include "nebulastore/common/dout.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <thread>
#include <cstring>

namespace nebulastore {

Logger* Logger::Instance() {
    static Logger instance;
    return &instance;
}

void Logger::Init(const std::string& log_file) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 初始化子系统日志级别
    for (size_t i = 0; i < kSubsysCount && i < sizeof(kSubsysConfig) / sizeof(kSubsysConfig[0]); ++i) {
        subsys_levels_[i] = kSubsysConfig[i].gather_level;
    }

    // 关闭之前的文件
    if (log_file_.is_open()) {
        log_file_.close();
    }

    // 打开新的日志文件
    if (!log_file.empty()) {
        log_file_.open(log_file, std::ios::app);
        use_file_ = log_file_.is_open();
    } else {
        use_file_ = false;
    }
}

bool Logger::ShouldGather(SubsysID subsys, int level) const {
    size_t idx = static_cast<size_t>(subsys);
    if (idx >= kSubsysCount) {
        return false;
    }
    uint8_t gather_level = subsys_levels_[idx];
    return level <= gather_level;
}

void Logger::WriteLog(SubsysID subsys, int level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 构建完整的日志行
    // 格式: "时间戳 [线程ID] [子系统] 级别 消息"
    std::ostringstream log_line;
    log_line << GetTimestamp() << " ";

    // 线程 ID (十六进制)
    std::ostringstream tid;
    tid << std::hex << std::this_thread::get_id() << std::dec;
    log_line << tid.str() << " ";

    // 子系统名称
    log_line << "[" << GetSubsysName(subsys) << "] ";

    // 日志级别
    log_line << level << " ";

    // 消息内容
    log_line << message;

    std::string log_str = log_line.str();

    // 输出到文件或控制台
    if (use_file_) {
        log_file_ << log_str << std::endl;
        log_file_.flush();  // 立即刷新
    } else {
        if (level < 0) {
            std::cerr << log_str << std::endl;
        } else {
            std::cout << log_str << std::endl;
        }
    }
}

void Logger::SetSubsysLevel(SubsysID subsys, uint8_t level) {
    size_t idx = static_cast<size_t>(subsys);
    if (idx < kSubsysCount) {
        std::lock_guard<std::mutex> lock(mutex_);
        subsys_levels_[idx] = level;
    }
}

uint8_t Logger::GetSubsysLevel(SubsysID subsys) const {
    size_t idx = static_cast<size_t>(subsys);
    if (idx < kSubsysCount) {
        return subsys_levels_[idx];
    }
    return default_level_;
}

std::string Logger::GetTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()) % 1000000;

    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_buf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(6) << ms.count();

    return oss.str();
}

const char* Logger::GetSubsysName(SubsysID subsys) const {
    size_t idx = static_cast<size_t>(subsys);
    if (idx < kSubsysCount) {
        size_t config_size = sizeof(kSubsysConfig) / sizeof(kSubsysConfig[0]);
        if (idx < config_size) {
            return kSubsysConfig[idx].name;
        }
    }
    return "unknown";
}

} // namespace nebulastore
