#include "order_match/core/order_book.hpp"

namespace order_match::core {

OrderBook::OrderBook(InstrumentConfig instrument) noexcept
    : instrument_(instrument) {}

const InstrumentConfig& OrderBook::instrument() const noexcept {
    return instrument_;
}

SequenceNumber OrderBook::sequence() const noexcept {
    return sequence_;
}

}  // namespace order_match::core
