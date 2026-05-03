#pragma once

#include "order_match/core/types.hpp"

#include <cstdint>

namespace order_match::core {

struct InstrumentConfig {
    PriceTicks min_price{};
    PriceTicks max_price{};
    QuantityUnits min_quantity{};
    QuantityUnits max_quantity{};
    std::uint32_t price_scale{};
    std::uint32_t quantity_scale{};
};

}  // namespace order_match::core
