#pragma once

#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace Aquarium::Common {

class CoroutineExecutor {
public:
    using UiPoster = std::function<void(std::function<void()>)>;

    explicit CoroutineExecutor(std::size_t workerCount = 2,
                               UiPoster uiPoster = nullptr)
        : _uiPoster(std::move(uiPoster)),
          _aliveFlag(std::make_shared<std::atomic<bool>>(true)) {
        if (workerCount == 0) {
            workerCount = 1;
        }
        _workers.reserve(workerCount);
        for (std::size_t i = 0; i < workerCount; ++i) {
            _workers.emplace_back(
                [this](std::stop_token st) { WorkerLoop(st); });
        }
    }

    ~CoroutineExecutor() {
        Stop();
    }

    CoroutineExecutor(const CoroutineExecutor&) = delete;
    CoroutineExecutor& operator=(const CoroutineExecutor&) = delete;
    CoroutineExecutor(CoroutineExecutor&&) = delete;
    CoroutineExecutor& operator=(CoroutineExecutor&&) = delete;

    void Stop() {
        _aliveFlag->store(false, std::memory_order_release);
        std::deque<std::coroutine_handle<>> pending;
        {
            std::scoped_lock lock(_mutex);
            if (_stopped.exchange(true, std::memory_order_acq_rel)) {
                return;
            }
            pending.swap(_queue);
        }
        _cv.notify_all();
        for (auto& worker : _workers) {
            worker.request_stop();
            if (worker.joinable()) {
                worker.join();
            }
        }
        for (auto handle : pending) {
            if (handle) {
                handle.destroy();
            }
        }
    }

    [[nodiscard]] bool IsStopped() const noexcept {
        return _stopped.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool HasUiPoster() const noexcept {
        return static_cast<bool>(_uiPoster);
    }

    struct BackgroundAwaiter {
        CoroutineExecutor* executor;

        [[nodiscard]] bool await_ready() const noexcept {
            return executor == nullptr || executor->IsStopped();
        }

        bool await_suspend(std::coroutine_handle<> handle) const {
            return executor->Enqueue(handle);
        }

        void await_resume() const noexcept {}
    };

    struct UiAwaiter {
        CoroutineExecutor* executor;

        [[nodiscard]] bool await_ready() const noexcept {
            return executor == nullptr || executor->IsStopped() ||
                   !executor->HasUiPoster();
        }

        bool await_suspend(std::coroutine_handle<> handle) const {
            return executor->PostToUi(handle);
        }

        void await_resume() const noexcept {}
    };

    [[nodiscard]] BackgroundAwaiter Background() noexcept {
        return BackgroundAwaiter{this};
    }

    [[nodiscard]] UiAwaiter Ui() noexcept {
        return UiAwaiter{this};
    }

private:
    struct PendingHandle {
        std::coroutine_handle<> handle;

        explicit PendingHandle(std::coroutine_handle<> h) noexcept
            : handle(h) {}

        ~PendingHandle() {
            if (handle) {
                handle.destroy();
            }
        }

        PendingHandle(const PendingHandle&) = delete;
        PendingHandle& operator=(const PendingHandle&) = delete;

        void Resume() noexcept {
            if (auto h = std::exchange(handle, nullptr)) {
                h.resume();
            }
        }
    };

    bool Enqueue(std::coroutine_handle<> handle) {
        {
            std::scoped_lock lock(_mutex);
            if (_stopped.load(std::memory_order_acquire)) {
                return false;
            }
            _queue.push_back(handle);
        }
        _cv.notify_one();
        return true;
    }

    bool PostToUi(std::coroutine_handle<> handle) {
        if (!_uiPoster || _stopped.load(std::memory_order_acquire)) {
            return false;
        }
        auto pending = std::make_shared<PendingHandle>(handle);
        auto alive = _aliveFlag;
        _uiPoster([pending = std::move(pending), alive = std::move(alive)] {
            if (alive->load(std::memory_order_acquire)) {
                pending->Resume();
            }
        });
        return true;
    }

    void WorkerLoop(std::stop_token stopToken) {
        while (!stopToken.stop_requested()) {
            std::coroutine_handle<> taskHandle;
            {
                std::unique_lock lock(_mutex);
                _cv.wait(lock, stopToken, [this, &stopToken] {
                    return stopToken.stop_requested() ||
                           _stopped.load(std::memory_order_acquire) ||
                           !_queue.empty();
                });

                if (stopToken.stop_requested() ||
                    _stopped.load(std::memory_order_acquire)) {
                    break;
                }

                if (_queue.empty()) {
                    continue;
                }
                taskHandle = _queue.front();
                _queue.pop_front();
            }

            if (taskHandle) {
                taskHandle.resume();
            }
        }
    }

    UiPoster _uiPoster;
    std::shared_ptr<std::atomic<bool>> _aliveFlag;
    std::atomic<bool> _stopped{false};
    std::mutex _mutex;
    std::condition_variable_any _cv;
    std::deque<std::coroutine_handle<>> _queue;
    std::vector<std::jthread> _workers;
};

[[nodiscard]] inline CoroutineExecutor::BackgroundAwaiter
ResumeBackground(CoroutineExecutor* executor) noexcept {
    return CoroutineExecutor::BackgroundAwaiter{executor};
}

[[nodiscard]] inline CoroutineExecutor::UiAwaiter
ResumeUi(CoroutineExecutor* executor) noexcept {
    return CoroutineExecutor::UiAwaiter{executor};
}

} // namespace Aquarium::Common
