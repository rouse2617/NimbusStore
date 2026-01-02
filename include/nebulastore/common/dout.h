#pragma once

#include "nebulastore/common/subsys.h"
#include <sstream>
#include <iostream>
#include <utility>

namespace nebulastore {

// 前置声明
class Logger;

// ================================
// LogEntry - 日志条目 (RAII 模式)
// ================================
class LogEntry {
    int level_;                    // 日志级别
    SubsysID subsys_;              // 子系统 ID
    std::ostringstream buffer_;    // 日志内容缓冲
    Logger* logger_;               // 日志器引用

public:
    LogEntry(int level, SubsysID subsys, Logger* logger)
        : level_(level), subsys_(subsys), logger_(logger) {
    }

    ~LogEntry() {
        // RAII: 析构时自动提交日志
        if (logger_) {
            logger_->WriteLog(subsys_, level_, buffer_.str());
        }
    }

    // 获取输出流，用于 << 操作符
    std::ostream& stream() {
        return buffer_;
    }

    // 禁止拷贝
    LogEntry(const LogEntry&) = delete;
    LogEntry& operator=(const LogEntry&) = delete;

    // 允许移动
    LogEntry(LogEntry&& other) noexcept
        : level_(other.level_), subsys_(other.subsys_),
          buffer_(std::move(other.buffer_)), logger_(other.logger_) {
        other.logger_ = nullptr;
    }

    LogEntry& operator=(LogEntry&& other) noexcept {
        if (this != &other) {
            level_ = other.level_;
            subsys_ = other.subsys_;
            buffer_ = std::move(other.buffer_);
            logger_ = other.logger_;
            other.logger_ = nullptr;
        }
        return *this;
    }
};

// ================================
// 日志宏定义 (参考 Ceph dout)
// ================================

// 检查是否应该收集日志，并创建 LogEntry
#define DOUT_IMPL(subsys, v)                                           \
    do {                                                                \
        if (auto* _logger = ::nebulastore::Logger::Instance();       \
            _logger->ShouldGather(::nebulastore::SubsysID::subsys, v)) { \
            ::nebulastore::LogEntry _entry(v, ::nebulastore::SubsysID::subsys, _logger); \
            std::ostream* _dout = &_entry.stream();

// 日志结束标记 (首先是 flush，然后结束语句块)
#define DENDL std::flush; \
    } \
    } while (0)

// 用户使用的 dendl (等价于 DENDL)
#define dendl DENDL

// 默认子系统日志
#define dout(v) DOUT_IMPL(_default, v) *_dout

// 指定子系统日志
#define subdout(sub, v) DOUT_IMPL(sub, v) *_dout

// 错误日志 (level = -1)
#define derr DOUT_IMPL(_default, -1) *_dout

// 警告日志 (level = 0)
#define dwarn DOUT_IMPL(_default, 0) *_dout

// 信息日志 (level = 1)
#define dinfo DOUT_IMPL(_default, 1) *_dout

} // namespace nebulastore
