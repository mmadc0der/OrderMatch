#pragma once

#include "order_match/core/instrument.hpp"
#include "order_match/core/order.hpp"
#include "order_match/core/types.hpp"

#include <deque>
#include <functional>
#include <map>
#include <unordered_map>

namespace order_match::core {

class OrderBook {
public:
    struct ActiveOrder {
        Side side{};
        PriceTicks price{};
        QuantityUnits remaining{};
    };

    using PriceLevel = std::deque<OrderId>;
    using BidLevels = std::map<PriceTicks, PriceLevel, std::greater<>>;
    using AskLevels = std::map<PriceTicks, PriceLevel>;

    explicit OrderBook(InstrumentConfig instrument = {}) noexcept;

    [[nodiscard]] const InstrumentConfig& instrument() const noexcept;
    [[nodiscard]] SequenceNumber sequence() const noexcept;
    [[nodiscard]] SubmitResult submit(const OrderRequest& request);
    [[nodiscard]] CancelResult cancel(OrderId order_id) noexcept;
    [[nodiscard]] BookSnapshot snapshot(DepthLimit depth) const;

private:
    InstrumentConfig instrument_{};
    SequenceNumber sequence_{};
    OrderId next_order_id_{1U};
    std::unordered_map<OrderId, ActiveOrder> active_orders_{};
    BidLevels bids_{};
    AskLevels asks_{};
};

}  // namespace order_match::core
