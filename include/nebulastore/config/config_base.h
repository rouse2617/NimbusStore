#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <string>
#include <type_traits>
#include <vector>

#include "nebulastore/common/result.h"
#include "nebulastore/common/types.h"

namespace nebulastore {
namespace config {

// ============================================================================
// Type Traits
// ============================================================================

template <typename T>
struct is_vector : std::false_type {};
template <typename T>
struct is_vector<std::vector<T>> : std::true_type {};
template <typename T>
inline constexpr bool is_vector_v = is_vector<T>::value;

template <typename T>
inline constexpr bool IsPrimitive = std::is_trivially_copyable_v<T> && sizeof(T) <= 8;

template <typename T>
using ReturnType = std::conditional_t<IsPrimitive<T>, T, const T&>;

template <typename T>
using ValueType = std::conditional_t<std::is_same_v<T, const char*>, std::string, T>;

// ============================================================================
// AtomicValue<T> - 基础类型原子存储
// ============================================================================

template <typename T>
class AtomicValue {
public:
    AtomicValue() = default;
    explicit AtomicValue(T value) : value_(value) {}
    AtomicValue(const AtomicValue& o) : value_(o.value()) {}

    T value(std::memory_order order = std::memory_order_seq_cst) const {
        return value_.load(order);
    }

    void setValue(T value, std::memory_order order = std::memory_order_seq_cst) {
        value_.store(value, order);
    }

    AtomicValue& operator=(const AtomicValue& o) {
        setValue(o.value());
        return *this;
    }

private:
    std::atomic<T> value_{};
};

// ============================================================================
// TLSStore<T> - 复杂类型 TLS 缓存存储
// ============================================================================

template <typename T>
class TLSStore {
public:
    explicit TLSStore(T&& value) : ptr_(std::make_shared<T>(std::move(value))) {}
    TLSStore(const TLSStore& o) : ptr_(std::atomic_load(&o.ptr_)) {}

    const T& value() const {
        thread_local struct Cache {
            std::shared_ptr<const T> ptr;
            uint64_t version = 0;
        } cache;

        uint64_t latest = version_.load(std::memory_order_acquire);
        if (cache.version != latest) {
            cache.ptr = std::atomic_load(&ptr_);
            cache.version = latest;
        }
        return *cache.ptr;
    }

    template <typename V>
    void setValue(V&& value) {
        std::atomic_store(&ptr_, std::make_shared<T>(std::forward<V>(value)));
        ++version_;
    }

    TLSStore& operator=(const TLSStore& o) {
        std::atomic_store(&ptr_, std::atomic_load(&o.ptr_));
        ++version_;
        return *this;
    }

private:
    std::atomic<uint64_t> version_{1};
    std::shared_ptr<T> ptr_;
};

template <typename T>
using StoreType = std::conditional_t<IsPrimitive<T>, AtomicValue<T>, TLSStore<T>>;

// ============================================================================
// IItem - 配置项接口
// ============================================================================

struct IItem {
    virtual ~IItem() = default;
    virtual Result<Void> validate(const std::string& path) const = 0;
    virtual bool supportHotUpdate() const = 0;
    virtual std::string toString() const = 0;
};

// ============================================================================
// Item<T> - 配置项实现
// ============================================================================

template <typename T>
class Item : public IItem {
public:
    using Checker = std::function<bool(ReturnType<T>)>;

    Item(std::string name, T defaultValue, bool hotUpdatable, Checker checker = nullptr)
        : value_(std::move(defaultValue)),
          name_(std::move(name)),
          hotUpdatable_(hotUpdatable),
          checker_(checker ? std::move(checker) : [](ReturnType<T>) { return true; }) {}

    ReturnType<T> value() const { return value_.value(); }

    template <typename V>
    void setValue(V&& value) { value_.setValue(std::forward<V>(value)); }

    bool checkAndSet(ReturnType<T> value) {
        if (checker_(value)) {
            setValue(value);
            return true;
        }
        return false;
    }

    Result<Void> validate(const std::string& path) const override {
        if (!checker_(value())) {
            return Err<Void>(ErrorCode::kInvalidArgument, "Check failed: " + path);
        }
        return Ok();
    }

    bool supportHotUpdate() const override { return hotUpdatable_; }

    std::string toString() const override {
        if constexpr (std::is_same_v<T, std::string>) {
            return value();
        } else if constexpr (std::is_same_v<T, bool>) {
            return value() ? "true" : "false";
        } else if constexpr (std::is_arithmetic_v<T>) {
            return std::to_string(value());
        } else {
            return "<complex>";
        }
    }

private:
    StoreType<T> value_;
    std::string name_;
    bool hotUpdatable_;
    Checker checker_;
};

// ============================================================================
// IConfig - 配置接口
// ============================================================================

struct IConfig {
    virtual ~IConfig() = default;
    virtual Result<Void> validate(const std::string& path = {}) const = 0;
};

// ============================================================================
// ConfigBase<Derived> - CRTP 配置基类
// ============================================================================

template <typename Derived>
class ConfigBase : public IConfig {
protected:
    ConfigBase() = default;
    ConfigBase(const ConfigBase&) = default;
    ConfigBase& operator=(const ConfigBase&) = default;

public:
    Result<Void> validate(const std::string& path = {}) const override {
        auto* self = static_cast<const Derived*>(this);
        for (const auto& [name, item] : items_) {
            auto fullPath = path.empty() ? name : path + "." + name;
            auto res = (self->*item).validate(fullPath);
            if (res.hasError()) return res;
        }
        for (const auto& [name, section] : sections_) {
            auto fullPath = path.empty() ? name : path + "." + name;
            auto res = (self->*section).validate(fullPath);
            if (res.hasError()) return res;
        }
        return Ok();
    }

    Derived clone() const {
        std::shared_lock lock(mutex_);
        return static_cast<const Derived&>(*this);
    }

protected:
    mutable std::shared_mutex mutex_;
    std::map<std::string, IItem Derived::*, std::less<>> items_;
    std::map<std::string, IConfig Derived::*, std::less<>> sections_;
};

}  // namespace config

// ============================================================================
// 配置项宏定义
// ============================================================================

#define CONFIG_ADD_ITEM(name, defaultValue, hotUpdatable, ...)                              \
private:                                                                                    \
    using T##name = ::nebulastore::config::ValueType<std::decay_t<decltype(defaultValue)>>; \
    using R##name = ::nebulastore::config::ReturnType<T##name>;                             \
public:                                                                                     \
    R##name name() const { return name##_.value(); }                                        \
    bool set_##name(R##name value) { return name##_.checkAndSet(value); }                   \
private:                                                                                    \
    ::nebulastore::config::Item<T##name> name##_ = ::nebulastore::config::Item<T##name>(    \
        #name, defaultValue, [this] {                                                       \
            using Self = std::decay_t<decltype(*this)>;                                     \
            ConfigBase<Self>::items_[#name] =                                               \
                reinterpret_cast<::nebulastore::config::IItem Self::*>(&Self::name##_);     \
            return hotUpdatable;                                                            \
        }() __VA_OPT__(, ) __VA_ARGS__)

#define CONFIG_ITEM(name, defaultValue, ...) \
    CONFIG_ADD_ITEM(name, defaultValue, false, __VA_ARGS__)

#define CONFIG_HOT_UPDATED_ITEM(name, defaultValue, ...) \
    CONFIG_ADD_ITEM(name, defaultValue, true, __VA_ARGS__)

#define CONFIG_OBJ(name, cls)                                                               \
public:                                                                                     \
    cls& name() { return name##_; }                                                         \
    const cls& name() const { return name##_; }                                             \
private:                                                                                    \
    cls name##_ = [this]() -> cls {                                                         \
        using Self = std::decay_t<decltype(*this)>;                                         \
        ConfigBase<Self>::sections_[#name] =                                                \
            reinterpret_cast<::nebulastore::config::IConfig Self::*>(&Self::name##_);       \
        return cls{};                                                                       \
    }()

// ============================================================================
// 常用 Checker 函数
// ============================================================================

namespace nebulastore::config::checkers {

template <typename T>
bool checkPositive(T val) { return val > 0; }

template <typename T>
bool checkNotNegative(T val) { return val >= 0; }

template <typename T>
bool checkNotEmpty(const T& c) { return !c.empty(); }

template <typename T, T Min, T Max>
bool checkRange(T val) { return val >= Min && val <= Max; }

}  // namespace nebulastore::config::checkers

}  // namespace nebulastore
