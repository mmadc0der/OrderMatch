#pragma once

#include <cstddef>

namespace order_match::engine {

template <typename Event>
class EventBus {
public:
    EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    [[nodiscard]] bool try_push(const Event& event);
    [[nodiscard]] bool try_pop(Event& event);
    [[nodiscard]] std::size_t capacity() const noexcept;
};

}  // namespace order_match::engine
