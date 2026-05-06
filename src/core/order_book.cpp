#include "order_match/core/order_book.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace order_match::core {

namespace {

[[nodiscard]] constexpr bool is_valid_side(const Side side) noexcept {
    return side == Side::buy || side == Side::sell;
}

[[nodiscard]] constexpr bool is_valid_type(const OrderType type) noexcept {
    return type == OrderType::limit || type == OrderType::market;
}

[[nodiscard]] constexpr bool is_valid_tif(const TimeInForce tif) noexcept {
    return tif == TimeInForce::gtc || tif == TimeInForce::ioc || tif == TimeInForce::fok;
}

[[nodiscard]] constexpr bool has_price_limits(const InstrumentConfig& instrument) noexcept {
    return instrument.min_price != 0U || instrument.max_price != 0U;
}

[[nodiscard]] constexpr bool has_quantity_limits(const InstrumentConfig& instrument) noexcept {
    return instrument.min_quantity != 0U || instrument.max_quantity != 0U;
}

[[nodiscard]] constexpr bool price_in_range(
    const InstrumentConfig& instrument,
    const PriceTicks price) noexcept {
    if (!has_price_limits(instrument)) {
        return true;
    }

    return price >= instrument.min_price && price <= instrument.max_price;
}

[[nodiscard]] constexpr bool quantity_in_range(
    const InstrumentConfig& instrument,
    const QuantityUnits quantity) noexcept {
    if (!has_quantity_limits(instrument)) {
        return true;
    }

    return quantity >= instrument.min_quantity && quantity <= instrument.max_quantity;
}

[[nodiscard]] constexpr bool crosses_limit(
    const Side incoming_side,
    const PriceTicks incoming_price,
    const PriceTicks resting_price) noexcept {
    if (incoming_side == Side::buy) {
        return resting_price <= incoming_price;
    }

    return resting_price >= incoming_price;
}

template <typename Levels>
[[nodiscard]] QuantityUnits available_liquidity(
    const Levels& levels,
    const Side incoming_side,
    const OrderType type,
    const PriceTicks price,
    const std::unordered_map<OrderId, OrderBook::ActiveOrder>& active_orders) {
    QuantityUnits available{};

    for (const auto& [level_price, order_ids] : levels) {
        if (type == OrderType::limit && !crosses_limit(incoming_side, price, level_price)) {
            break;
        }

        for (const OrderId order_id : order_ids) {
            const auto order_it = active_orders.find(order_id);
            if (order_it == active_orders.end() || order_it->second.remaining == 0U) {
                continue;
            }

            available += order_it->second.remaining;
        }
    }

    return available;
}

template <typename Levels>
void prune_front(
    Levels& levels,
    const std::unordered_map<OrderId, OrderBook::ActiveOrder>& active_orders) {
    for (auto level_it = levels.begin(); level_it != levels.end(); ) {
        auto& order_ids = level_it->second;

        while (!order_ids.empty()) {
            const OrderId order_id = order_ids.front();
            const auto order_it = active_orders.find(order_id);
            if (order_it != active_orders.end() && order_it->second.remaining != 0U) {
                break;
            }

            order_ids.pop_front();
        }

        if (order_ids.empty()) {
            level_it = levels.erase(level_it);
        } else {
            ++level_it;
        }
    }
}

template <typename Levels>
void match_against_levels(
    Levels& levels,
    const Side incoming_side,
    const OrderType type,
    const PriceTicks price,
    const OrderId taker_order_id,
    const SequenceNumber sequence,
    QuantityUnits& remaining,
    QuantityUnits& matched,
    std::vector<Trade>& fills,
    std::unordered_map<OrderId, OrderBook::ActiveOrder>& active_orders) {
    for (auto level_it = levels.begin(); level_it != levels.end() && remaining != 0U; ) {
        const auto level_price = level_it->first;
        if (type == OrderType::limit && !crosses_limit(incoming_side, price, level_price)) {
            break;
        }

        auto& order_ids = level_it->second;
        while (!order_ids.empty() && remaining != 0U) {
            const OrderId maker_id = order_ids.front();
            const auto maker_it = active_orders.find(maker_id);
            if (maker_it == active_orders.end() || maker_it->second.remaining == 0U) {
                order_ids.pop_front();
                continue;
            }

            const QuantityUnits traded = std::min(remaining, maker_it->second.remaining);
            remaining -= traded;
            matched += traded;
            fills.push_back(Trade{
                taker_order_id,
                maker_id,
                pack_price_quantity(level_price, traded),
                sequence,
            });
            maker_it->second.remaining -= traded;

            if (maker_it->second.remaining == 0U) {
                active_orders.erase(maker_it);
                order_ids.pop_front();
            }
        }

        if (order_ids.empty()) {
            level_it = levels.erase(level_it);
        } else {
            ++level_it;
        }
    }
}

[[nodiscard]] BookSnapshot make_snapshot(
    const OrderBook::BidLevels& bids,
    const OrderBook::AskLevels& asks,
    const std::unordered_map<OrderId, OrderBook::ActiveOrder>& active_orders,
    const SequenceNumber sequence,
    const DepthLimit depth) {
    BookSnapshot snapshot{};
    snapshot.sequence = sequence;

    const auto append_levels = [&active_orders, depth](const auto& levels, std::vector<BookLevel>& output) {
        output.reserve(depth == 0U ? 0U : static_cast<std::size_t>(depth));

        if (depth == 0U) {
            return;
        }

        DepthLimit emitted{};
        for (const auto& [price, order_ids] : levels) {
            if (emitted == depth) {
                break;
            }

            QuantityUnits total_quantity{};
            std::uint32_t order_count{};
            for (const OrderId order_id : order_ids) {
                const auto order_it = active_orders.find(order_id);
                if (order_it == active_orders.end() || order_it->second.remaining == 0U) {
                    continue;
                }

                total_quantity += order_it->second.remaining;
                ++order_count;
            }

            if (order_count == 0U) {
                continue;
            }

            output.push_back(BookLevel{price, total_quantity, order_count});
            ++emitted;
        }
    };

    append_levels(bids, snapshot.bids);
    append_levels(asks, snapshot.asks);

    return snapshot;
}

}  // namespace

OrderBook::OrderBook(InstrumentConfig instrument) noexcept
    : instrument_(instrument) {}

const InstrumentConfig& OrderBook::instrument() const noexcept {
    return instrument_;
}

SequenceNumber OrderBook::sequence() const noexcept {
    return sequence_;
}

SubmitResult OrderBook::submit(const OrderRequest& request) {
    SubmitResult result{};

    if (!is_valid_side(request.side) || !is_valid_type(request.type) || !is_valid_tif(request.time_in_force) ||
        request.quantity == 0U || !quantity_in_range(instrument_, request.quantity) ||
        (request.type == OrderType::limit && (request.price == 0U || !price_in_range(instrument_, request.price)))) {
        result.reject_reason = SubmitRejectReason::invalid_request;
        return result;
    }

    const bool is_buy = request.side == Side::buy;
    const bool can_rest = request.type == OrderType::limit && request.time_in_force == TimeInForce::gtc;

    QuantityUnits available{};
    if (is_buy) {
        available = available_liquidity(asks_, request.side, request.type, request.price, active_orders_);
    } else {
        available = available_liquidity(bids_, request.side, request.type, request.price, active_orders_);
    }

    if (request.time_in_force == TimeInForce::fok && available < request.quantity) {
        result.reject_reason = SubmitRejectReason::insufficient_liquidity;
        return result;
    }

    const OrderId order_id = next_order_id_++;
    const SequenceNumber mutation_sequence = sequence_ + 1U;
    QuantityUnits remaining = request.quantity;
    QuantityUnits matched{};
    std::vector<Trade> fills{};

    if (is_buy) {
        match_against_levels(
            asks_,
            request.side,
            request.type,
            request.price,
            order_id,
            mutation_sequence,
            remaining,
            matched,
            fills,
            active_orders_);
    } else {
        match_against_levels(
            bids_,
            request.side,
            request.type,
            request.price,
            order_id,
            mutation_sequence,
            remaining,
            matched,
            fills,
            active_orders_);
    }

    if (remaining != 0U && can_rest) {
        active_orders_[order_id] = ActiveOrder{
            request.side,
            request.price,
            remaining,
        };
        if (is_buy) {
            bids_[request.price].push_back(order_id);
        } else {
            asks_[request.price].push_back(order_id);
        }
    }

    sequence_ = mutation_sequence;
    result.order_id = order_id;
    result.accepted = true;
    result.resting = remaining != 0U && can_rest;
    result.matched_quantity = matched;
    result.resting_quantity = result.resting ? remaining : 0U;
    result.sequence = mutation_sequence;
    result.fills = std::move(fills);
    return result;
}

CancelResult OrderBook::cancel(OrderId order_id) noexcept {
    CancelResult result{};
    result.order_id = order_id;

    const auto order_it = active_orders_.find(order_id);
    if (order_it == active_orders_.end()) {
        return result;
    }

    const Side side = order_it->second.side;
    result.status = CancelStatus::cancelled;
    result.cancelled_quantity = order_it->second.remaining;
    active_orders_.erase(order_it);

    sequence_ += 1U;
    result.sequence = sequence_;

    if (side == Side::buy) {
        prune_front(bids_, active_orders_);
    } else {
        prune_front(asks_, active_orders_);
    }
    return result;
}

BookSnapshot OrderBook::snapshot(DepthLimit depth) const {
    return make_snapshot(bids_, asks_, active_orders_, sequence_, depth);
}

}  // namespace order_match::core
