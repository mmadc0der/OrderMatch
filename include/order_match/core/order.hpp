#pragma once

#include "order_match/core/types.hpp"

#include <vector>

namespace order_match::core {

enum class Side : std::uint8_t {
    buy = 0U,
    sell = 1U,
};

enum class OrderType : std::uint8_t {
    limit = 0U,
    market = 1U,
};

enum class TimeInForce : std::uint8_t {
    gtc = 0U,
    ioc = 1U,
    fok = 2U,
};

namespace order_flags {
inline constexpr OrderFlags side_sell = 1U << 0U;  // Unset means buy.
}  // namespace order_flags

struct OrderRequest {
    Side side{Side::buy};
    OrderType type{OrderType::limit};
    TimeInForce time_in_force{TimeInForce::gtc};
    PriceTicks price{};
    QuantityUnits quantity{};
};

struct RestingOrder {
    OrderId id{};
    PriceQuantity price_quantity{};
    SequenceNumber sequence{};
    OrderFlags flags{};
};

using Order = RestingOrder;

struct Trade {
    OrderId taker_order_id{};
    OrderId maker_order_id{};
    PriceQuantity price_quantity{};
    SequenceNumber sequence{};
};

struct BookLevel {
    PriceTicks price{};
    QuantityUnits quantity{};
    std::uint32_t order_count{};
};

struct BookSnapshot {
    SequenceNumber sequence{};
    std::vector<BookLevel> bids{};
    std::vector<BookLevel> asks{};
};

struct SubmitResult {
    OrderId order_id{no_order_id};
    bool accepted{};
    bool resting{};
    QuantityUnits matched_quantity{};
    QuantityUnits resting_quantity{};
    SequenceNumber sequence{};
};

enum class CancelStatus : std::uint8_t {
    cancelled = 0U,
    not_found = 1U,
    already_terminal = 2U,
};

struct CancelResult {
    OrderId order_id{no_order_id};
    CancelStatus status{CancelStatus::not_found};
    QuantityUnits cancelled_quantity{};
    SequenceNumber sequence{};
};

}  // namespace order_match::core
