#pragma once

#include <coroutine>
#include <deque>
#include <mutex>
#include <optional>
#include <condition_variable>
#include <queue>

namespace nebulastore {

template<typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t capacity) : capacity_(capacity) {}

    bool enqueue(T item) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.size() >= capacity_) {
            cv_not_full_.wait(lock, [this] { return queue_.size() < capacity_; });
        }
        queue_.push_back(std::move(item));
        if (!dequeue_waiters_.empty()) {
            auto h = dequeue_waiters_.front();
            dequeue_waiters_.pop();
            lock.unlock();
            h.resume();
        } else {
            cv_not_empty_.notify_one();
        }
        return true;
    }

    std::optional<T> dequeue() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_not_empty_.wait(lock, [this] { return !queue_.empty(); });
        T item = std::move(queue_.front());
        queue_.pop_front();
        if (!enqueue_waiters_.empty()) {
            auto h = enqueue_waiters_.front();
            enqueue_waiters_.pop();
            lock.unlock();
            h.resume();
        } else {
            cv_not_full_.notify_one();
        }
        return item;
    }

    std::optional<T> try_dequeue() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop_front();
        cv_not_full_.notify_one();
        return item;
    }

    std::optional<T> try_steal() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        T item = std::move(queue_.back());
        queue_.pop_back();
        cv_not_full_.notify_one();
        return item;
    }

    struct CoEnqueueAwaiter {
        BoundedQueue& q;
        T item;

        bool await_ready() {
            std::lock_guard<std::mutex> lock(q.mutex_);
            return q.queue_.size() < q.capacity_;
        }

        void await_suspend(std::coroutine_handle<> h) {
            std::lock_guard<std::mutex> lock(q.mutex_);
            if (q.queue_.size() < q.capacity_) {
                h.resume();
            } else {
                q.enqueue_waiters_.push(h);
            }
        }

        void await_resume() {
            std::lock_guard<std::mutex> lock(q.mutex_);
            q.queue_.push_back(std::move(item));
            q.cv_not_empty_.notify_one();
        }
    };

    CoEnqueueAwaiter co_enqueue(T item) {
        return CoEnqueueAwaiter{*this, std::move(item)};
    }

    struct CoDequeueAwaiter {
        BoundedQueue& q;
        std::optional<T> result;

        bool await_ready() {
            std::lock_guard<std::mutex> lock(q.mutex_);
            if (!q.queue_.empty()) {
                result = std::move(q.queue_.front());
                q.queue_.pop_front();
                q.cv_not_full_.notify_one();
                return true;
            }
            return false;
        }

        void await_suspend(std::coroutine_handle<> h) {
            std::lock_guard<std::mutex> lock(q.mutex_);
            if (!q.queue_.empty()) {
                result = std::move(q.queue_.front());
                q.queue_.pop_front();
                q.cv_not_full_.notify_one();
                h.resume();
            } else {
                q.dequeue_waiters_.push(h);
            }
        }

        T await_resume() {
            if (result) return std::move(*result);
            std::lock_guard<std::mutex> lock(q.mutex_);
            T item = std::move(q.queue_.front());
            q.queue_.pop_front();
            q.cv_not_full_.notify_one();
            return item;
        }
    };

    CoDequeueAwaiter co_dequeue() {
        return CoDequeueAwaiter{*this, std::nullopt};
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_not_empty_;
    std::condition_variable cv_not_full_;
    std::deque<T> queue_;
    size_t capacity_;
    std::queue<std::coroutine_handle<>> enqueue_waiters_;
    std::queue<std::coroutine_handle<>> dequeue_waiters_;
};

} // namespace nebulastore
