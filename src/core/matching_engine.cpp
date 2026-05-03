#include "order_match/core/matching_engine.hpp"

namespace order_match::core {

const OrderBook& MatchingEngine::book() const noexcept {
    return book_;
}

OrderBook& MatchingEngine::book() noexcept {
    return book_;
}

}  // namespace order_match::core
