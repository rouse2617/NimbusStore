#pragma once

#include <coroutine>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>

namespace nebulastore {

class Semaphore {
public:
    explicit Semaphore(int count = 0) : count_(count) {}

    void signal() {
        std::unique_lock<std::mutex> lock(mutex_);
        ++count_;
        if (!waiters_.empty()) {
            auto handle = waiters_.front();
            waiters_.pop();
            lock.unlock();
            handle.resume();
        } else {
            cv_.notify_one();
        }
    }

    void wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return count_ > 0; });
        --count_;
    }

    bool try_wait() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_ > 0) {
            --count_;
            return true;
        }
        return false;
    }

    struct CoWaitAwaiter {
        Semaphore& sem;

        bool await_ready() {
            return sem.try_wait();
        }

        void await_suspend(std::coroutine_handle<> handle) {
            std::lock_guard<std::mutex> lock(sem.mutex_);
            if (sem.count_ > 0) {
                --sem.count_;
                handle.resume();
            } else {
                sem.waiters_.push(handle);
            }
        }

        void await_resume() {}
    };

    CoWaitAwaiter co_wait() {
        return CoWaitAwaiter{*this};
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    int count_;
    std::queue<std::coroutine_handle<>> waiters_;
};

} // namespace nebulastore
