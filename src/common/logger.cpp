// ================================
// 简单日志实现
// ================================

#include "nebulastore/common/logger.h"
#include <iostream>
#include <ctime>

namespace nebulastore {

Logger* Logger::Instance() {
    static Logger instance;
    return &instance;
}

void Logger::Init(const std::string& log_file, const std::string& level) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!log_file.empty()) {
        file_stream_.open(log_file, std::ios::app);
        use_file_ = file_stream_.is_open();
    }
    (void)level;  // 暂不使用
}

void Logger::Init(const std::string& log_file) {
    Init(log_file, "info");
}

bool Logger::ShouldGather(SubsysID subsys, int level) const {
    auto idx = static_cast<size_t>(subsys);
    if (idx < kSubsysCount) {
        return level <= static_cast<int>(kSubsysConfig[idx].gather_level);
    }
    return level <= log_level_;
}

void Logger::WriteLog(SubsysID subsys, int level, const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* level_str = level <= -1 ? "ERROR" : level == 0 ? "WARN" : "INFO";
    auto idx = static_cast<size_t>(subsys);
    const char* subsys_name = idx < kSubsysCount ? kSubsysConfig[idx].name : "unknown";

    std::ostream& out = use_file_ ? file_stream_ : (level <= 0 ? std::cerr : std::cout);
    out << "[" << level_str << "] [" << subsys_name << "] " << msg << std::endl;
}

void Logger::Info(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string log_msg = "[INFO] " + msg;
    if (use_file_) {
        file_stream_ << log_msg << std::endl;
    } else {
        std::cout << log_msg << std::endl;
    }
}

void Logger::Warn(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string log_msg = "[WARN] " + msg;
    if (use_file_) {
        file_stream_ << log_msg << std::endl;
    } else {
        std::cerr << log_msg << std::endl;
    }
}

void Logger::Error(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string log_msg = "[ERROR] " + msg;
    if (use_file_) {
        file_stream_ << log_msg << std::endl;
    } else {
        std::cerr << log_msg << std::endl;
    }
}

void Logger::Debug(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string log_msg = "[DEBUG] " + msg;
    if (use_file_) {
        file_stream_ << log_msg << std::endl;
    } else {
        std::cout << log_msg << std::endl;
    }
}

} // namespace nebulastore
