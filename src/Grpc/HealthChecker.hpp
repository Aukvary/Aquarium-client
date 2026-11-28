#pragma once

#include "./BackendClient.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <stop_token>
#include <thread>

namespace Aquarium::Grpc {

class HealthChecker {
public:
    using HealthCallback =
        std::function<void(const Result<aquarium::api::HealthResponse>&)>;
    HealthChecker(BackendClient& client,
                  std::chrono::milliseconds interval,
                  HealthCallback callback);

    ~HealthChecker();

    HealthChecker(const HealthChecker&) = delete;
    HealthChecker& operator=(const HealthChecker&) = delete;

    void Start();
    void Stop();
    void TriggerCheck();

    [[nodiscard]] bool IsRunning() const noexcept;

private:
    BackendClient& _client;
    std::chrono::milliseconds _interval;
    HealthCallback _callback;

    std::mutex _mutex;
    std::condition_variable_any _cv;
    std::atomic<bool> _running{false};
    bool _triggerNow{false};
    std::jthread _workerThread;

    void RunLoop(std::stop_token stopToken);
};

} // namespace Aquarium::Grpc
