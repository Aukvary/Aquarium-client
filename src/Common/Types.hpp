#pragma once

#include <coroutine>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace Aquarium::Common {
using TaskId = std::int64_t;
using UserId = std::int64_t;

enum class AppErrorCode : std::uint8_t {
    OK = 0,
    UNAUTHENTICATED,
    PERMISSION_DENIED,
    NOT_FOUND,
    UNAVAILABLE,
    INVALID_ARGUMENT,
    INTERNAL_ERROR,
};

[[nodiscard]] constexpr std::string_view ErrorCodeToString(AppErrorCode code) {
    switch (code) {
    case AppErrorCode::OK:
        return "OK";
    case AppErrorCode::UNAUTHENTICATED:
        return "UNAUTHENTICATED";
    case AppErrorCode::PERMISSION_DENIED:
        return "PERMISSION_DENIED";
    case AppErrorCode::NOT_FOUND:
        return "NOT_FOUND";
    case AppErrorCode::UNAVAILABLE:
        return "UNAVAILABLE";
    case AppErrorCode::INVALID_ARGUMENT:
        return "INVALID_ARGUMENT";
    case AppErrorCode::INTERNAL_ERROR:
        return "INTERNAL_ERROR";
    }
    return "UNKNOWN_ERROR";
}

struct AppError {
    AppErrorCode code{AppErrorCode::OK};
    std::string message;
    std::string details;

    [[nodiscard]] bool IsOk() const noexcept {
        return code == AppErrorCode::OK;
    }

    [[nodiscard]] bool IsAuthError() const noexcept {
        return code == AppErrorCode::UNAUTHENTICATED;
    }

    [[nodiscard]] bool IsPermissionDenied() const noexcept {
        return code == AppErrorCode::PERMISSION_DENIED;
    }

    [[nodiscard]] bool IsNotFound() const noexcept {
        return code == AppErrorCode::NOT_FOUND;
    }

    [[nodiscard]] bool IsUnavailable() const noexcept {
        return code == AppErrorCode::UNAVAILABLE;
    }

    [[nodiscard]] std::string ToString() const {
        if (IsOk()) {
            return "OK";
        }
        std::ostringstream oss;
        oss << "[" << ErrorCodeToString(code) << "] " << message;
        if (!details.empty()) {
            oss << " (" << details << ")";
        }
        return oss.str();
    }

    bool operator==(const AppError& other) const = default;
};

template <typename T>
class Result {
private:
    std::variant<T, AppError> _data;

public:
    explicit Result(T value) : _data(std::move(value)) {}
    explicit Result(AppError error) : _data(std::move(error)) {}

    [[nodiscard]] bool HasValue() const noexcept {
        return std::holds_alternative<T>(_data);
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return HasValue();
    }

    [[nodiscard]] const T& Value() const {
        return std::get<T>(_data);
    }

    [[nodiscard]] T& Value() {
        return std::get<T>(_data);
    }

    [[nodiscard]] const AppError& Error() const {
        return std::get<AppError>(_data);
    }

    [[nodiscard]] T ValueOr(T fallback) const {
        if (HasValue()) {
            return Value();
        }
        return fallback;
    }
};

template <>
class Result<void> {
private:
    AppError _error;

public:
    Result() : _error(AppError{}) {}
    explicit Result(AppError error) : _error(std::move(error)) {}

    [[nodiscard]] bool HasValue() const noexcept {
        return _error.IsOk();
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return HasValue();
    }

    [[nodiscard]] const AppError& Error() const noexcept {
        return _error;
    }
};

template <typename T>
class Task {
private:
    struct Promise {
        std::optional<T> value;
        std::coroutine_handle<> continuation;

        Task get_return_object() noexcept {
            return Task{std::coroutine_handle<Promise>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept {
            return {};
        }

        auto final_suspend() noexcept {
            struct FinalAwaiter {
                [[nodiscard]] bool await_ready() const noexcept {
                    return false;
                }

                std::coroutine_handle<>
                await_suspend(std::coroutine_handle<Promise> h) noexcept {
                    return h.promise().continuation ? h.promise().continuation
                                                    : std::noop_coroutine();
                }

                void await_resume() noexcept {}
            };

            return FinalAwaiter{};
        }

        void return_value(T v) {
            value.emplace(std::move(v));
        }

        void unhandled_exception() {
            std::terminate();
        }
    };

    std::coroutine_handle<Promise> _handle;

public:
    using promise_type = Promise;

    explicit Task(std::coroutine_handle<Promise> h) noexcept : _handle(h) {}
    Task(Task&& other) noexcept
        : _handle(std::exchange(other._handle, nullptr)) {}
    ~Task() {
        if (_handle) {
            _handle.destroy();
        }
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task& operator=(Task&&) = delete;

    [[nodiscard]] bool await_ready() const noexcept {
        return false;
    }

    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> caller) noexcept {
        _handle.promise().continuation = caller;
        return _handle;
    }

    T await_resume() {
        return std::move(*_handle.promise().value);
    }

    [[nodiscard]] T Get() {
        if (!_handle.done()) {
            _handle.resume();
        }
        return std::move(*_handle.promise().value);
    }
};

struct DetachedTask {
    struct promise_type {
        DetachedTask get_return_object() noexcept {
            return {};
        }

        std::suspend_never initial_suspend() noexcept {
            return {};
        }

        std::suspend_never final_suspend() noexcept {
            return {};
        }

        void return_void() noexcept {}

        void unhandled_exception() noexcept {
            std::terminate();
        }
    };
};
} // namespace Aquarium::Common
