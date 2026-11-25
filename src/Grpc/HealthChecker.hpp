#pragma once

#include "./BackendClient.hpp"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

namespace Aquarium::Grpc {

class HealthChecker {
    using HealthCallback =
        std::function<void(const Result<aquarium::api::HealthResponse>&)>;

private:
    void RunLoop(std::stop_token stopToken);

    std::shared_ptr<BackendClient> _client;
    std::chrono::milliseconds _interval;
    HealthCallback _callback;

    std::mutex _mutex;
    std::condition_variable_any _cv;
    std::jthread _workerThread;
    bool _running{false};
    bool _triggerNow{false};

public:
    HealthChecker(std::shared_ptr<BackendClient> client,
                  std::chrono::milliseconds interval,
                  HealthCallback callback);

    ~HealthChecker();

    HealthChecker(const HealthChecker&) = delete;
    HealthChecker& operator=(const HealthChecker&) = delete;

    void Start();
    void Stop();
    void TriggerCheck();

    [[nodiscard]] bool IsRunning() const noexcept;
};

} // namespace Aquarium::Grpc
