#pragma once

#include "order_match/core/order_book.hpp"

namespace order_match::core {

class MatchingEngine {
public:
    MatchingEngine() = default;

    [[nodiscard]] const OrderBook& book() const noexcept;
    [[nodiscard]] OrderBook& book() noexcept;

private:
    OrderBook book_{};
};

}  // namespace order_match::core
