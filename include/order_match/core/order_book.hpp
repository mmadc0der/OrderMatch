#pragma once

#include "order_match/core/instrument.hpp"
#include "order_match/core/order.hpp"
#include "order_match/core/types.hpp"

namespace order_match::core {

struct MatchResult {
    OrderId order_id{};
    QuantityUnits remaining_quantity{};
    SequenceNumber sequence{};
};

class OrderBook {
public:
    explicit OrderBook(InstrumentConfig instrument = {}) noexcept;

    [[nodiscard]] const InstrumentConfig& instrument() const noexcept;
    [[nodiscard]] SequenceNumber sequence() const noexcept;
    [[nodiscard]] SubmitResult submit(const OrderRequest& request);
    [[nodiscard]] CancelResult cancel(OrderId order_id) noexcept;
    [[nodiscard]] BookSnapshot snapshot(DepthLimit depth) const;

private:
    InstrumentConfig instrument_{};
    SequenceNumber sequence_{};
};

}  // namespace order_match::core
