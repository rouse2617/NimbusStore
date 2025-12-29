// ================================
// 空实现骨架 - 等你来完善
// ================================

#include "yig/common/logger.h"

namespace yig {

Logger* Logger::Instance() {
    static Logger instance;
    return &instance;
}

void Logger::Init(const std::string& log_file, const std::string& level) {
    // TODO: 初始化 spdlog
    // logger_ = spdlog::basic_logger_mt("yig", log_file);
    // logger_->set_level(spdlog::level::from_str(level));
}

} // namespace yig
