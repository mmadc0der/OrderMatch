#pragma once

#include "order_match/core/types.hpp"

#include <cstdint>

namespace order_match::engine {

using EventType = std::uint8_t;
using EventFlags = std::uint8_t;
using ResultCode = std::uint8_t;

namespace event_type {
inline constexpr EventType submit_order = 1U;
inline constexpr EventType cancel_order = 2U;
}  // namespace event_type

namespace event_flags {
inline constexpr EventFlags side_sell = 1U << 0U;    // Unset means buy.
inline constexpr EventFlags kind_market = 1U << 1U;  // Unset means limit.
inline constexpr EventFlags tif_ioc = 1U << 2U;
inline constexpr EventFlags tif_fok = 1U << 3U;
}  // namespace event_flags

namespace result_code {
inline constexpr ResultCode ok = 0U;
inline constexpr ResultCode rejected = 1U;
inline constexpr ResultCode malformed_request = 2U;
inline constexpr ResultCode invalid_price = 3U;
inline constexpr ResultCode invalid_quantity = 4U;
inline constexpr ResultCode invalid_order_type = 5U;
inline constexpr ResultCode invalid_time_in_force = 6U;
inline constexpr ResultCode not_found = 7U;
inline constexpr ResultCode already_terminal = 8U;
inline constexpr ResultCode insufficient_liquidity = 9U;
inline constexpr ResultCode overloaded = 10U;
}  // namespace result_code

struct InboundEvent {
    core::RequestId request_id{};
    core::OrderId order_id{};
    core::PriceQuantity price_quantity{};
    EventType type{};
    EventFlags flags{};
    std::uint16_t reserved{};
};

}  // namespace order_match::engine
