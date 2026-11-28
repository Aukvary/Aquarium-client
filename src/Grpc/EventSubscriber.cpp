#include "./EventSubscriber.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <utility>

namespace Aquarium::Grpc {

EventSubscriber::EventSubscriber(BackendClient& client,
                                 EventCallback eventCallback,
                                 ReconnectCallback reconnectCallback)
    : _client(client), _eventCallback(std::move(eventCallback)),
      _reconnectCallback(std::move(reconnectCallback)) {}

EventSubscriber::~EventSubscriber() {
    Stop();
}

void EventSubscriber::Start() {
    std::scoped_lock lock(_mutex);
    if (_running) {
        return;
    }
    _running = true;
    _workerThread = std::jthread([this](std::stop_token st) { RunLoop(st); });
}

void EventSubscriber::Stop() {
    _workerThread.request_stop();
    {
        std::scoped_lock lock(_mutex);
        if (!_running) {
            return;
        }
        _running = false;
        if (_activeContext != nullptr) {
            _activeContext->TryCancel();
        }
    }
    _cv.notify_all();
    if (_workerThread.joinable()) {
        _workerThread.join();
    }
}

bool EventSubscriber::IsRunning() const noexcept {
    return _running.load(std::memory_order_acquire);
}

void EventSubscriber::RunLoop(std::stop_token stopToken) {
    std::chrono::milliseconds backoff(1000);
    const std::chrono::milliseconds maxBackoff(30000);
    bool isInitial = true;

    while (!stopToken.stop_requested()) {
        std::unique_ptr<grpc::ClientContext> context;
        context = _client.CreateStreamContext();

        if (context) {
            {
                std::scoped_lock lock(_mutex);
                if (stopToken.stop_requested()) {
                    break;
                }
                _activeContext = context.get();
            }

            auto reader = _client.SubscribeEvents(context.get());
            if (reader) {
                if (!isInitial && _reconnectCallback) {
                    _reconnectCallback();
                }
                isInitial = false;

                aquarium::api::TaskEvent event;
                while (!stopToken.stop_requested() && reader->Read(&event)) {
                    backoff = std::chrono::milliseconds(1000);
                    if (_eventCallback) {
                        _eventCallback(event);
                    }
                }
                reader->Finish();
            }

            {
                std::scoped_lock lock(_mutex);
                _activeContext = nullptr;
            }
        }

        if (stopToken.stop_requested()) {
            break;
        }

        std::unique_lock lock(_mutex);
        _cv.wait_for(lock, stopToken, backoff,
                     [&stopToken] { return stopToken.stop_requested(); });

        backoff = std::min(backoff * 2, maxBackoff);
    }
}

} // namespace Aquarium::Grpc
