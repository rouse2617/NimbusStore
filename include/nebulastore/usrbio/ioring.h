#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace nebulastore {
namespace usrbio {

// IO 参数结构
struct IoArgs {
    uint32_t buf_id;      // 缓冲区 ID
    uint32_t buf_off;     // 缓冲区内偏移
    uint64_t file_iid;    // 文件 inode ID
    uint64_t file_off;    // 文件内偏移
    uint64_t io_len;      // IO 长度
    void* userdata;       // 用户数据
};

// 提交队列条目
struct IoSqe {
    uint32_t index;       // 条目索引
    uint32_t reserved;    // 保留字段
    void* userdata;       // 用户数据
};

// 完成队列条目
struct IoCqe {
    uint32_t index;       // 条目索引
    int32_t result;       // IO 结果 (>=0 成功字节数, <0 错误码)
    void* userdata;       // 用户数据
};

// 无锁环形队列
template <typename T>
class LockFreeRing {
public:
    explicit LockFreeRing(uint32_t capacity)
        : capacity_(capacity), mask_(capacity - 1),
          head_(0), tail_(0), entries_(capacity) {
        // capacity 必须是 2 的幂
    }

    bool Push(const T& entry) {
        uint32_t tail = tail_.load(std::memory_order_relaxed);
        uint32_t next = (tail + 1) & mask_;
        if (next == head_.load(std::memory_order_acquire)) {
            return false;  // 队列满
        }
        entries_[tail] = entry;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    bool Pop(T& entry) {
        uint32_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) {
            return false;  // 队列空
        }
        entry = entries_[head];
        head_.store((head + 1) & mask_, std::memory_order_release);
        return true;
    }

    uint32_t Count() const {
        uint32_t head = head_.load(std::memory_order_acquire);
        uint32_t tail = tail_.load(std::memory_order_acquire);
        return (tail - head) & mask_;
    }

    uint32_t Capacity() const { return capacity_ - 1; }

private:
    uint32_t capacity_;
    uint32_t mask_;
    std::atomic<uint32_t> head_;
    std::atomic<uint32_t> tail_;
    std::vector<T> entries_;
};

// IoRing 核心类
class IoRing {
public:
    explicit IoRing(uint32_t entries, bool for_read = true)
        : sq_(RoundUpPow2(entries + 1)),
          cq_(RoundUpPow2(entries + 1)),
          for_read_(for_read),
          io_args_(RoundUpPow2(entries + 1)),
          next_index_(0) {}

    // 添加提交队列条目
    int AddSqe(const IoArgs& args) {
        uint32_t idx = next_index_.fetch_add(1, std::memory_order_relaxed) % io_args_.size();
        io_args_[idx] = args;

        IoSqe sqe;
        sqe.index = idx;
        sqe.reserved = 0;
        sqe.userdata = args.userdata;

        if (!sq_.Push(sqe)) {
            return -1;  // 队列满
        }
        return static_cast<int>(idx);
    }

    // 消费完成队列条目
    bool PopCqe(IoCqe& cqe) {
        return cq_.Pop(cqe);
    }

    // 完成一个 IO (由 IO 处理线程调用)
    bool CompleteSqe(uint32_t index, int32_t result, void* userdata) {
        IoCqe cqe;
        cqe.index = index;
        cqe.result = result;
        cqe.userdata = userdata;
        return cq_.Push(cqe);
    }

    // 获取待处理的 SQE
    bool PopSqe(IoSqe& sqe) {
        return sq_.Pop(sqe);
    }

    // 获取 IO 参数
    const IoArgs& GetIoArgs(uint32_t index) const {
        return io_args_[index % io_args_.size()];
    }

    uint32_t SqeCount() const { return sq_.Count(); }
    uint32_t CqeCount() const { return cq_.Count(); }
    bool IsForRead() const { return for_read_; }

private:
    static uint32_t RoundUpPow2(uint32_t v) {
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        return v + 1;
    }

    LockFreeRing<IoSqe> sq_;  // 提交队列
    LockFreeRing<IoCqe> cq_;  // 完成队列
    bool for_read_;
    std::vector<IoArgs> io_args_;
    std::atomic<uint32_t> next_index_;
};

}  // namespace usrbio
}  // namespace nebulastore
