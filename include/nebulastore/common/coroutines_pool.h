#pragma once

#include <coroutine>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <random>
#include "nebulastore/common/bounded_queue.h"
#include "nebulastore/common/async.h"

namespace nebulastore {

template<typename Job>
class CoroutinesPool {
public:
    struct Config {
        size_t num_workers = std::thread::hardware_concurrency();
        size_t queue_size = 1024;
    };

    explicit CoroutinesPool(Config config = {})
        : config_(config), running_(false) {
        for (size_t i = 0; i < config_.num_workers; ++i) {
            queues_.emplace_back(std::make_unique<BoundedQueue<Job>>(config_.queue_size));
        }
    }

    ~CoroutinesPool() { stop(); }

    void start() {
        if (running_.exchange(true)) return;
        for (size_t i = 0; i < config_.num_workers; ++i) {
            workers_.emplace_back([this, i] { worker_loop(i); });
        }
    }

    void stop() {
        if (!running_.exchange(false)) return;
        for (auto& w : workers_) {
            if (w.joinable()) w.join();
        }
        workers_.clear();
    }

    bool submit(Job job) {
        if (!running_) return false;
        size_t idx = next_queue_.fetch_add(1) % config_.num_workers;
        return queues_[idx]->enqueue(std::move(job));
    }

    bool submit_to(size_t worker_id, Job job) {
        if (!running_ || worker_id >= config_.num_workers) return false;
        return queues_[worker_id]->enqueue(std::move(job));
    }

    struct CoSubmitAwaiter {
        CoroutinesPool& pool;
        Job job;
        size_t idx;

        bool await_ready() { return false; }

        void await_suspend(std::coroutine_handle<> h) {
            pool.queues_[idx]->enqueue(std::move(job));
            h.resume();
        }

        void await_resume() {}
    };

    CoSubmitAwaiter co_submit(Job job) {
        size_t idx = next_queue_.fetch_add(1) % config_.num_workers;
        return CoSubmitAwaiter{*this, std::move(job), idx};
    }

    size_t num_workers() const { return config_.num_workers; }

private:
    void worker_loop(size_t id) {
        thread_local std::mt19937 rng(std::random_device{}());
        auto& local_queue = *queues_[id];

        while (running_) {
            auto job = local_queue.try_dequeue();
            if (!job) {
                job = try_steal(id, rng);
            }
            if (job) {
                (*job)();
            } else {
                std::this_thread::yield();
            }
        }
        // Drain remaining jobs
        while (auto job = local_queue.try_dequeue()) {
            (*job)();
        }
    }

    std::optional<Job> try_steal(size_t self_id, std::mt19937& rng) {
        if (config_.num_workers <= 1) return std::nullopt;
        std::uniform_int_distribution<size_t> dist(0, config_.num_workers - 2);
        size_t victim = dist(rng);
        if (victim >= self_id) ++victim;
        return queues_[victim]->try_steal();
    }

    Config config_;
    std::atomic<bool> running_;
    std::atomic<size_t> next_queue_{0};
    std::vector<std::unique_ptr<BoundedQueue<Job>>> queues_;
    std::vector<std::thread> workers_;
};

// 便捷类型别名
using TaskPool = CoroutinesPool<std::function<void()>>;

} // namespace nebulastore
