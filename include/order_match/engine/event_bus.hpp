#pragma once

#include <condition_variable>
#include <deque>
#include <cstddef>
#include <mutex>
#include <utility>

namespace order_match::engine {

template <typename Event>
class EventBus {
public:
    explicit EventBus(std::size_t capacity = 1024U) noexcept
        : capacity_(capacity == 0U ? 1U : capacity) {}
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    [[nodiscard]] bool try_push(const Event& event);
    [[nodiscard]] bool try_push(Event&& event);
    [[nodiscard]] bool try_pop(Event& event);
    [[nodiscard]] bool wait_pop(Event& event);
    void close() noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;

private:
    mutable std::mutex mutex_{};
    std::condition_variable cv_{};
    std::deque<Event> queue_{};
    std::size_t capacity_{};
    bool closed_{false};
};

template <typename Event>
bool EventBus<Event>::try_push(const Event& event) {
    std::scoped_lock lock(mutex_);
    if (closed_ || queue_.size() >= capacity_) {
        return false;
    }

    queue_.push_back(event);
    cv_.notify_one();
    return true;
}

template <typename Event>
bool EventBus<Event>::try_push(Event&& event) {
    std::scoped_lock lock(mutex_);
    if (closed_ || queue_.size() >= capacity_) {
        return false;
    }

    queue_.push_back(std::move(event));
    cv_.notify_one();
    return true;
}

template <typename Event>
bool EventBus<Event>::try_pop(Event& event) {
    std::scoped_lock lock(mutex_);
    if (queue_.empty()) {
        return false;
    }

    event = std::move(queue_.front());
    queue_.pop_front();
    return true;
}

template <typename Event>
bool EventBus<Event>::wait_pop(Event& event) {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [this]() noexcept { return closed_ || !queue_.empty(); });
    if (queue_.empty()) {
        return false;
    }

    event = std::move(queue_.front());
    queue_.pop_front();
    return true;
}

template <typename Event>
void EventBus<Event>::close() noexcept {
    std::scoped_lock lock(mutex_);
    closed_ = true;
    cv_.notify_all();
}

template <typename Event>
std::size_t EventBus<Event>::capacity() const noexcept {
    return capacity_;
}

}  // namespace order_match::engine
