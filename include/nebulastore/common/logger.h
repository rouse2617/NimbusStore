#pragma once

#include <spdlog/spdlog.h>
#include <memory>

namespace nebulastore {

// ================================
// 日志系统
// ================================

class Logger {
public:
    static Logger* Instance();

    void Init(const std::string& log_file, const std::string& level = "info");

    std::shared_ptr<spdlog::logger> logger() { return logger_; }

private:
    Logger() = default;
    std::shared_ptr<spdlog::logger> logger_;
};

#define LOG_INFO(fmt, ...) spdlog::info(fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) spdlog::warn(fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) spdlog::error(fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) spdlog::debug(fmt, ##__VA_ARGS__)

} // namespace nebulastore
