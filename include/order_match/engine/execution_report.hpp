#pragma once

#include "order_match/core/order.hpp"
#include "order_match/engine/event.hpp"

#include <vector>

namespace order_match::engine {

enum class ExecutionStatus : std::uint8_t {
    none = 0U,
    resting = 1U,
    filled = 2U,
    cancelled = 3U,
};

struct ExecutionFill {
    core::OrderId taker_order_id{};
    core::OrderId maker_order_id{};
    core::PriceQuantity price_quantity{};
    core::SequenceNumber sequence{};
};

struct ExecutionReport {
    core::RequestId request_id{};
    core::OrderId order_id{core::no_order_id};
    core::QuantityUnits cumulative_quantity{};
    core::QuantityUnits leaves_quantity{};
    core::SequenceNumber sequence{};
    ResultCode result{result_code::ok};
    ExecutionStatus status{ExecutionStatus::none};
    std::vector<ExecutionFill> fills{};
};

}  // namespace order_match::engine
