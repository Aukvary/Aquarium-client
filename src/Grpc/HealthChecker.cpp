#include "./HealthChecker.hpp"

#include <utility>

namespace Aquarium::Grpc {

HealthChecker::HealthChecker(BackendClient& client,
                             std::chrono::milliseconds interval,
                             HealthCallback callback)
    : _client(client), _interval(interval), _callback(std::move(callback)) {}

HealthChecker::~HealthChecker() {
    Stop();
}

void HealthChecker::Start() {
    std::scoped_lock lock(_mutex);
    if (_running) {
        return;
    }
    _triggerNow = false;
    _running = true;
    _workerThread = std::jthread([this](std::stop_token st) { RunLoop(st); });
}

void HealthChecker::Stop() {
    _workerThread.request_stop();
    {
        std::scoped_lock lock(_mutex);
        if (!_running) {
            return;
        }
        _running = false;
    }
    _cv.notify_all();
    if (_workerThread.joinable()) {
        _workerThread.join();
    }
}

void HealthChecker::TriggerCheck() {
    {
        std::scoped_lock lock(_mutex);
        _triggerNow = true;
    }
    _cv.notify_one();
}

bool HealthChecker::IsRunning() const noexcept {
    return _running.load(std::memory_order_acquire);
}

void HealthChecker::RunLoop(std::stop_token stopToken) {
    while (!stopToken.stop_requested()) {
        if (_callback) {
            auto result = _client.Health();
            _callback(result);
        }

        std::unique_lock lock(_mutex);
        _cv.wait_for(lock, stopToken, _interval, [this, &stopToken] {
            return stopToken.stop_requested() || _triggerNow;
        });

        if (stopToken.stop_requested()) {
            break;
        }

        _triggerNow = false;
    }
}

} // namespace Aquarium::Grpc
