#pragma once

#include <cstdint>

namespace order_match::core {

using PriceTicks = std::uint32_t;
using QuantityUnits = std::uint32_t;
using PriceQuantity = std::uint64_t;
using OrderId = std::uint64_t;
using RequestId = std::uint64_t;
using SequenceNumber = std::uint64_t;
using OrderFlags = std::uint8_t;
using DepthLimit = std::uint32_t;

inline constexpr OrderId no_order_id = 0U;

[[nodiscard]] constexpr PriceQuantity pack_price_quantity(
    const PriceTicks price,
    const QuantityUnits quantity) noexcept {
    return (static_cast<PriceQuantity>(price) << 32U) | quantity;
}

[[nodiscard]] constexpr PriceTicks unpack_price(const PriceQuantity value) noexcept {
    return static_cast<PriceTicks>(value >> 32U);
}

[[nodiscard]] constexpr QuantityUnits unpack_quantity(const PriceQuantity value) noexcept {
    return static_cast<QuantityUnits>(value);
}

}  // namespace order_match::core
