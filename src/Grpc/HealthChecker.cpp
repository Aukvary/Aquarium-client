#include "./HealthChecker.hpp"

#include <utility>

namespace Aquarium::Grpc {

HealthChecker::HealthChecker(std::shared_ptr<BackendClient> client,
                             std::chrono::milliseconds interval,
                             HealthCallback callback)
    : _client(std::move(client)), _interval(interval),
      _callback(std::move(callback)) {}

HealthChecker::~HealthChecker() {
    Stop();
}

void HealthChecker::Start() {
    std::scoped_lock lock(_mutex);
    if (_running) {
        return;
    }
    _running = true;
    _workerThread = std::jthread([this](std::stop_token st) { RunLoop(st); });
}

void HealthChecker::Stop() {
    {
        std::scoped_lock lock(_mutex);
        if (!_running) {
            return;
        }
        _running = false;
    }
    if (_workerThread.joinable()) {
        _workerThread.request_stop();
        _cv.notify_all();
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
    return _running;
}

void HealthChecker::RunLoop(std::stop_token stopToken) {
    while (!stopToken.stop_requested()) {
        if (_client && _callback) {
            auto result = _client->Health();
            _callback(result);
        }

        std::unique_lock<std::mutex> lock(_mutex);
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
