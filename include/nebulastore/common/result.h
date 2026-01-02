#pragma once

#include <variant>
#include <type_traits>
#include <utility>
#include "nebulastore/common/types.h"

namespace nebulastore {

// Void type for Result<Void> when no value is needed
struct Void {};

template <typename T>
class [[nodiscard]] Result {
public:
    Result(T value) : data_(std::move(value)) {}
    Result(Status error) : data_(std::move(error)) {}

    [[nodiscard]] bool hasValue() const { return std::holds_alternative<T>(data_); }
    [[nodiscard]] bool hasError() const { return std::holds_alternative<Status>(data_); }

    [[nodiscard]] T& value() & { return std::get<T>(data_); }
    [[nodiscard]] const T& value() const& { return std::get<T>(data_); }
    [[nodiscard]] T&& value() && { return std::get<T>(std::move(data_)); }

    [[nodiscard]] Status& error() & { return std::get<Status>(data_); }
    [[nodiscard]] const Status& error() const& { return std::get<Status>(data_); }

    // map: Result<T> -> Result<U> via f: T -> U
    template <typename F>
    [[nodiscard]] auto map(F&& f) const& -> Result<std::invoke_result_t<F, const T&>> {
        if (hasValue()) return std::forward<F>(f)(value());
        return error();
    }

    template <typename F>
    [[nodiscard]] auto map(F&& f) && -> Result<std::invoke_result_t<F, T&&>> {
        if (hasValue()) return std::forward<F>(f)(std::move(*this).value());
        return error();
    }

    // andThen: Result<T> -> Result<U> via f: T -> Result<U>
    template <typename F>
    [[nodiscard]] auto andThen(F&& f) const& -> std::invoke_result_t<F, const T&> {
        if (hasValue()) return std::forward<F>(f)(value());
        return error();
    }

    template <typename F>
    [[nodiscard]] auto andThen(F&& f) && -> std::invoke_result_t<F, T&&> {
        if (hasValue()) return std::forward<F>(f)(std::move(*this).value());
        return error();
    }

    // orElse: handle error case
    template <typename F>
    [[nodiscard]] Result orElse(F&& f) const& {
        if (hasError()) return std::forward<F>(f)(error());
        return *this;
    }

    template <typename F>
    [[nodiscard]] Result orElse(F&& f) && {
        if (hasError()) return std::forward<F>(f)(error());
        return std::move(*this);
    }

private:
    std::variant<T, Status> data_;
};

// Factory functions
template <typename T>
[[nodiscard]] Result<std::decay_t<T>> Ok(T&& value) {
    return Result<std::decay_t<T>>(std::forward<T>(value));
}

[[nodiscard]] inline Result<Void> Ok() {
    return Result<Void>(Void{});
}

template <typename T = Void>
[[nodiscard]] Result<T> Err(Status error) {
    return Result<T>(std::move(error));
}

template <typename T = Void>
[[nodiscard]] Result<T> Err(ErrorCode code, const std::string& msg = "") {
    return Result<T>(Status(code, msg));
}

} // namespace nebulastore
