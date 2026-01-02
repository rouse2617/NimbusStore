#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <unordered_map>
#include <optional>

namespace nebulastore {

template<typename T, typename Key = std::string>
class SingleFlight {
public:
    using Func = std::function<T()>;

    T Do(const Key& key, Func fn) {
        std::shared_ptr<Call> c;
        {
            std::unique_lock lock(mu_);
            auto it = calls_.find(key);
            if (it != calls_.end()) {
                c = it->second;
                lock.unlock();
                return wait(c);
            }
            c = std::make_shared<Call>();
            calls_[key] = c;
        }

        try {
            c->val = fn();
        } catch (...) {
            c->err = std::current_exception();
        }

        {
            std::lock_guard lock(mu_);
            calls_.erase(key);
        }

        {
            std::lock_guard lock(c->mu);
            c->done = true;
        }
        c->cv.notify_all();

        if (c->err) std::rethrow_exception(c->err);
        return std::move(c->val);
    }

    std::optional<T> TryPiggyback(const Key& key) {
        std::shared_ptr<Call> c;
        {
            std::shared_lock lock(mu_);
            auto it = calls_.find(key);
            if (it == calls_.end()) return std::nullopt;
            c = it->second;
        }
        return wait(c);
    }

    void Forget(const Key& key) {
        std::lock_guard lock(mu_);
        calls_.erase(key);
    }

private:
    struct Call {
        std::mutex mu;
        std::condition_variable cv;
        bool done = false;
        T val{};
        std::exception_ptr err;
    };

    T wait(std::shared_ptr<Call> c) {
        std::unique_lock lock(c->mu);
        c->cv.wait(lock, [&] { return c->done; });
        if (c->err) std::rethrow_exception(c->err);
        return c->val;
    }

    std::shared_mutex mu_;
    std::unordered_map<Key, std::shared_ptr<Call>> calls_;
};

} // namespace nebulastore
