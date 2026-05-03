#include "order_match/core/order_book.hpp"

#include <algorithm>
#include <utility>

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
    const std::unordered_map<OrderId, OrderBook::OrderRecord>& orders) {
    QuantityUnits available{};

    for (const auto& [level_price, order_ids] : levels) {
        if (type == OrderType::limit && !crosses_limit(incoming_side, price, level_price)) {
            break;
        }

        for (const OrderId order_id : order_ids) {
            const auto order_it = orders.find(order_id);
            if (order_it == orders.end() || order_it->second.state != OrderBook::OrderState::resting) {
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
    std::unordered_map<OrderId, OrderBook::OrderRecord>& orders) {
    for (auto level_it = levels.begin(); level_it != levels.end(); ) {
        auto& order_ids = level_it->second;

        while (!order_ids.empty()) {
            const OrderId order_id = order_ids.front();
            const auto order_it = orders.find(order_id);
            if (order_it != orders.end() && order_it->second.state == OrderBook::OrderState::resting &&
                order_it->second.remaining != 0U) {
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
    QuantityUnits& remaining,
    QuantityUnits& matched,
    std::unordered_map<OrderId, OrderBook::OrderRecord>& orders) {
    for (auto level_it = levels.begin(); level_it != levels.end() && remaining != 0U; ) {
        const auto level_price = level_it->first;
        if (type == OrderType::limit && !crosses_limit(incoming_side, price, level_price)) {
            break;
        }

        auto& order_ids = level_it->second;
        while (!order_ids.empty() && remaining != 0U) {
            const OrderId maker_id = order_ids.front();
            const auto maker_it = orders.find(maker_id);
            if (maker_it == orders.end() || maker_it->second.state != OrderBook::OrderState::resting ||
                maker_it->second.remaining == 0U) {
                order_ids.pop_front();
                continue;
            }

            const QuantityUnits traded = std::min(remaining, maker_it->second.remaining);
            remaining -= traded;
            matched += traded;
            maker_it->second.remaining -= traded;

            if (maker_it->second.remaining == 0U) {
                maker_it->second.state = OrderBook::OrderState::filled;
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
    const std::unordered_map<OrderId, OrderBook::OrderRecord>& orders,
    const SequenceNumber sequence,
    const DepthLimit depth) {
    BookSnapshot snapshot{};
    snapshot.sequence = sequence;

    const auto append_levels = [&orders, depth](const auto& levels, std::vector<BookLevel>& output) {
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
                const auto order_it = orders.find(order_id);
                if (order_it == orders.end() || order_it->second.state != OrderBook::OrderState::resting ||
                    order_it->second.remaining == 0U) {
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
        return result;
    }

    const bool is_buy = request.side == Side::buy;
    const bool can_rest = request.type == OrderType::limit && request.time_in_force == TimeInForce::gtc;

    QuantityUnits available{};
    if (is_buy) {
        available = available_liquidity(asks_, request.side, request.type, request.price, orders_);
    } else {
        available = available_liquidity(bids_, request.side, request.type, request.price, orders_);
    }

    if (request.time_in_force == TimeInForce::fok && available < request.quantity) {
        return result;
    }

    const OrderId order_id = next_order_id_++;
    auto& record = orders_[order_id];
    record.side = request.side;
    record.type = request.type;
    record.time_in_force = request.time_in_force;
    record.price = request.price;
    record.remaining = request.quantity;
    record.state = OrderState::resting;

    QuantityUnits remaining = request.quantity;
    QuantityUnits matched{};

    if (is_buy) {
        match_against_levels(asks_, request.side, request.type, request.price, remaining, matched, orders_);
    } else {
        match_against_levels(bids_, request.side, request.type, request.price, remaining, matched, orders_);
    }

    if (remaining != 0U && can_rest) {
        if (is_buy) {
            bids_[request.price].push_back(order_id);
        } else {
            asks_[request.price].push_back(order_id);
        }
        record.remaining = remaining;
        record.state = OrderState::resting;
    } else {
        record.remaining = 0U;
        record.state = OrderState::filled;
    }

    sequence_ += 1U;
    result.order_id = order_id;
    result.accepted = true;
    result.resting = remaining != 0U && can_rest;
    result.matched_quantity = matched;
    result.resting_quantity = result.resting ? remaining : 0U;
    result.sequence = sequence_;
    return result;
}

CancelResult OrderBook::cancel(OrderId order_id) noexcept {
    CancelResult result{};
    result.order_id = order_id;

    const auto order_it = orders_.find(order_id);
    if (order_it == orders_.end()) {
        return result;
    }

    if (order_it->second.state == OrderState::filled || order_it->second.state == OrderState::cancelled) {
        result.status = CancelStatus::already_terminal;
        return result;
    }

    if (order_it->second.state != OrderState::resting) {
        result.status = CancelStatus::not_found;
        return result;
    }

    const PriceTicks price = order_it->second.price;
    bool found_level = false;
    if (order_it->second.side == Side::buy) {
        found_level = bids_.find(price) != bids_.end();
    } else {
        found_level = asks_.find(price) != asks_.end();
    }
    if (!found_level) {
        order_it->second.state = OrderState::cancelled;
        order_it->second.remaining = 0U;
        result.status = CancelStatus::already_terminal;
        return result;
    }

    order_it->second.state = OrderState::cancelled;
    result.status = CancelStatus::cancelled;
    result.cancelled_quantity = order_it->second.remaining;
    order_it->second.remaining = 0U;
    sequence_ += 1U;
    result.sequence = sequence_;

    if (order_it->second.side == Side::buy) {
        prune_front(bids_, orders_);
    } else {
        prune_front(asks_, orders_);
    }
    return result;
}

BookSnapshot OrderBook::snapshot(DepthLimit depth) const {
    return make_snapshot(bids_, asks_, orders_, sequence_, depth);
}

}  // namespace order_match::core
