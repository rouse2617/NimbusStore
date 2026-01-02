#pragma once

#include <coroutine>
#include <optional>
#include <exception>
#include <type_traits>
#include <utility>
#include "nebulastore/common/types.h"

namespace nebulastore {

// ================================
// 简单的 C++20 协程封装
// ================================

template<typename T>
class AsyncTask {
public:
    struct promise_type {
        AsyncTask get_return_object() {
            return AsyncTask{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        void return_value(T value) {
            value_ = std::move(value);
        }

        void unhandled_exception() {
            exception_ = std::current_exception();
        }

        T value_;
        std::exception_ptr exception_;
    };

    explicit AsyncTask(std::coroutine_handle<promise_type> handle)
        : handle_(handle) {}

    ~AsyncTask() {
        if (handle_) {
            handle_.destroy();
        }
    }

    // 禁止拷贝
    AsyncTask(const AsyncTask&) = delete;
    AsyncTask& operator=(const AsyncTask&) = delete;

    // 支持移动
    AsyncTask(AsyncTask&& other) noexcept
        : handle_(std::exchange(other.handle_, {})) {}

    AsyncTask& operator=(AsyncTask&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    // 获取结果
    T Get() {
        if (!handle_.done()) {
            handle_.resume();
        }

        if (handle_.promise().exception_) {
            std::rethrow_exception(handle_.promise().exception_);
        }

        return std::move(handle_.promise().value_);
    }

    // === awaitable 接口 ===
    bool await_ready() noexcept {
        return handle_.done();
    }

    void await_suspend(std::coroutine_handle<> continuation) {
        // TODO: 实现真正的异步调度
        // 当前: 直接 resume，同步执行
        handle_.resume();
        continuation.resume();
    }

    T await_resume() {
        if (handle_.promise().exception_) {
            std::rethrow_exception(handle_.promise().exception_);
        }
        return std::move(handle_.promise().value_);
    }

private:
    std::coroutine_handle<promise_type> handle_;
};

// void 特化
template<>
class AsyncTask<void> {
public:
    struct promise_type {
        AsyncTask get_return_object() {
            return AsyncTask{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        void return_void() {}

        void unhandled_exception() {
            exception_ = std::current_exception();
        }

        std::exception_ptr exception_;
    };

    explicit AsyncTask(std::coroutine_handle<promise_type> handle)
        : handle_(handle) {}

    ~AsyncTask() {
        if (handle_) {
            handle_.destroy();
        }
    }

    AsyncTask(const AsyncTask&) = delete;
    AsyncTask& operator=(const AsyncTask&) = delete;

    AsyncTask(AsyncTask&& other) noexcept
        : handle_(std::exchange(other.handle_, {})) {}

    AsyncTask& operator=(AsyncTask&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    void Get() {
        if (!handle_.done()) {
            handle_.resume();
        }

        if (handle_.promise().exception_) {
            std::rethrow_exception(handle_.promise().exception_);
        }
    }

    // === awaitable 接口 ===
    bool await_ready() noexcept {
        return handle_.done();
    }

    void await_suspend(std::coroutine_handle<> continuation) {
        // TODO: 实现真正的异步调度
        handle_.resume();
        continuation.resume();
    }

    void await_resume() {
        if (handle_.promise().exception_) {
            std::rethrow_exception(handle_.promise().exception_);
        }
    }

private:
    std::coroutine_handle<promise_type> handle_;
};

// ================================
// awaiter 包装器
// ================================

template<typename T>
class ReadyAwaiter {
public:
    explicit ReadyAwaiter(T value) : value_(std::move(value)) {}

    bool await_ready() noexcept { return true; }
    T await_resume() { return std::move(value_); }
    void await_suspend(std::coroutine_handle<>) {}

private:
    T value_;
};

template<typename T>
ReadyAwaiter<T> MakeReady(T value) {
    return ReadyAwaiter<T>{std::move(value)};
}

} // namespace nebulastore
