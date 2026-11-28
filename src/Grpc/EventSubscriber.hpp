#pragma once

#include "./BackendClient.hpp"

#include <condition_variable>
#include <functional>
#include <mutex>
#include <stop_token>
#include <TaskStoreService.pb.h>
#include <thread>

namespace Aquarium::Grpc {

class EventSubscriber {
public:
    using EventCallback = std::function<void(const aquarium::api::TaskEvent&)>;
    using ReconnectCallback = std::function<void()>;
    EventSubscriber(BackendClient& client,
                    EventCallback eventCallback,
                    ReconnectCallback reconnectCallback = nullptr);

    ~EventSubscriber();

    EventSubscriber(const EventSubscriber&) = delete;
    EventSubscriber& operator=(const EventSubscriber&) = delete;

    void Start();
    void Stop();

    [[nodiscard]] bool IsRunning() const noexcept;

private:
    BackendClient& _client;
    EventCallback _eventCallback;
    ReconnectCallback _reconnectCallback;

    mutable std::mutex _mutex;
    std::condition_variable_any _cv;
    std::jthread _workerThread;
    grpc::ClientContext* _activeContext{nullptr};
    std::atomic<bool> _running{false};
    void RunLoop(std::stop_token stopToken);
};

} // namespace Aquarium::Grpc
