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
    enum class OrderState : std::uint8_t {
        resting = 0U,
        filled = 1U,
        cancelled = 2U,
    };

    struct OrderRecord {
        Side side{};
        OrderType type{};
        TimeInForce time_in_force{};
        PriceTicks price{};
        QuantityUnits remaining{};
        OrderState state{OrderState::resting};
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
    std::unordered_map<OrderId, OrderRecord> orders_{};
    BidLevels bids_{};
    AskLevels asks_{};
};

}  // namespace order_match::core
