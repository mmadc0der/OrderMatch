#include "order_match/core/order_book.hpp"

#include <gtest/gtest.h>

namespace {

using namespace order_match::core;

[[nodiscard]] InstrumentConfig make_instrument() {
    InstrumentConfig instrument{};
    instrument.min_price = 1U;
    instrument.max_price = 1'000'000U;
    instrument.min_quantity = 1U;
    instrument.max_quantity = 1'000'000U;
    instrument.price_scale = 100U;
    instrument.quantity_scale = 100U;
    return instrument;
}

TEST(OrderBookTest, RejectedSubmitDoesNotMutateOrConsumeIds) {
    OrderBook book{make_instrument()};

    const auto before = book.sequence();
    const auto rejected = book.submit(OrderRequest{Side::buy, OrderType::limit, TimeInForce::gtc, 0U, 10U});
    const auto snapshot_before_accept = book.snapshot(10U);
    const auto accepted = book.submit(OrderRequest{Side::buy, OrderType::limit, TimeInForce::gtc, 100U, 10U});
    const auto snapshot_after_accept = book.snapshot(10U);

    EXPECT_FALSE(rejected.accepted);
    EXPECT_EQ(rejected.reject_reason, SubmitRejectReason::invalid_request);
    EXPECT_EQ(rejected.order_id, no_order_id);
    EXPECT_EQ(snapshot_before_accept.sequence, before);
    EXPECT_TRUE(snapshot_before_accept.bids.empty());
    EXPECT_TRUE(snapshot_before_accept.asks.empty());
    EXPECT_EQ(accepted.order_id, 1U);
    EXPECT_TRUE(accepted.accepted);
    EXPECT_TRUE(accepted.resting);
    EXPECT_EQ(book.sequence(), before + 1U);
    EXPECT_EQ(snapshot_after_accept.sequence, book.sequence());
    ASSERT_EQ(snapshot_after_accept.bids.size(), 1U);
    EXPECT_EQ(snapshot_after_accept.bids.front().price, 100U);
    EXPECT_EQ(snapshot_after_accept.bids.front().quantity, 10U);
    EXPECT_EQ(snapshot_after_accept.bids.front().order_count, 1U);
}

TEST(OrderBookTest, RestingLimitOrderIsVisible) {
    OrderBook book{make_instrument()};

    const auto result = book.submit(OrderRequest{Side::buy, OrderType::limit, TimeInForce::gtc, 100U, 10U});
    const auto snapshot = book.snapshot(10U);

    EXPECT_TRUE(result.accepted);
    EXPECT_TRUE(result.resting);
    EXPECT_EQ(result.matched_quantity, 0U);
    EXPECT_EQ(result.resting_quantity, 10U);
    EXPECT_TRUE(result.fills.empty());
    EXPECT_EQ(snapshot.sequence, book.sequence());
    ASSERT_EQ(snapshot.bids.size(), 1U);
    EXPECT_EQ(snapshot.bids.front().price, 100U);
    EXPECT_EQ(snapshot.bids.front().quantity, 10U);
    EXPECT_EQ(snapshot.bids.front().order_count, 1U);
    EXPECT_TRUE(snapshot.asks.empty());
}

TEST(OrderBookTest, MarketOrderConsumesLiquidityWithoutResting) {
    OrderBook book{make_instrument()};

    const auto resting = book.submit(OrderRequest{Side::sell, OrderType::limit, TimeInForce::gtc, 100U, 5U});
    const auto taker = book.submit(OrderRequest{Side::buy, OrderType::market, TimeInForce::ioc, 0U, 3U});
    const auto snapshot = book.snapshot(10U);

    EXPECT_TRUE(resting.accepted);
    EXPECT_TRUE(taker.accepted);
    EXPECT_FALSE(taker.resting);
    EXPECT_EQ(taker.matched_quantity, 3U);
    EXPECT_EQ(taker.resting_quantity, 0U);
    ASSERT_EQ(taker.fills.size(), 1U);
    EXPECT_EQ(taker.fills.front().maker_order_id, resting.order_id);
    EXPECT_EQ(taker.fills.front().taker_order_id, taker.order_id);
    EXPECT_EQ(taker.fills.front().price_quantity, pack_price_quantity(100U, 3U));
    EXPECT_EQ(snapshot.asks.front().price, 100U);
    EXPECT_EQ(snapshot.asks.front().quantity, 2U);
    EXPECT_TRUE(snapshot.bids.empty());
}

TEST(OrderBookTest, IocDoesNotRestResidualQuantity) {
    OrderBook book{make_instrument()};

    const auto ask = book.submit(OrderRequest{Side::sell, OrderType::limit, TimeInForce::gtc, 100U, 2U});
    const auto ioc = book.submit(OrderRequest{Side::buy, OrderType::limit, TimeInForce::ioc, 101U, 5U});
    const auto snapshot = book.snapshot(10U);

    EXPECT_TRUE(ask.accepted);
    EXPECT_TRUE(ioc.accepted);
    EXPECT_FALSE(ioc.resting);
    EXPECT_EQ(ioc.matched_quantity, 2U);
    EXPECT_EQ(ioc.resting_quantity, 0U);
    ASSERT_EQ(ioc.fills.size(), 1U);
    EXPECT_TRUE(snapshot.bids.empty());
    EXPECT_TRUE(snapshot.asks.empty());
}

TEST(OrderBookTest, FifoWithinPriceLevel) {
    OrderBook book{make_instrument()};

    const auto first = book.submit(OrderRequest{Side::buy, OrderType::limit, TimeInForce::gtc, 100U, 7U});
    const auto second = book.submit(OrderRequest{Side::buy, OrderType::limit, TimeInForce::gtc, 100U, 10U});
    const auto taker = book.submit(OrderRequest{Side::sell, OrderType::market, TimeInForce::ioc, 0U, 8U});
    const auto snapshot = book.snapshot(10U);
    const auto first_cancel = book.cancel(first.order_id);
    const auto second_cancel = book.cancel(second.order_id);
    const auto unknown_cancel = book.cancel(999U);

    EXPECT_TRUE(first.accepted);
    EXPECT_TRUE(second.accepted);
    EXPECT_TRUE(taker.accepted);
    EXPECT_EQ(taker.matched_quantity, 8U);
    ASSERT_EQ(taker.fills.size(), 2U);
    EXPECT_EQ(taker.fills[0].maker_order_id, first.order_id);
    EXPECT_EQ(taker.fills[0].price_quantity, pack_price_quantity(100U, 7U));
    EXPECT_EQ(taker.fills[1].maker_order_id, second.order_id);
    EXPECT_EQ(taker.fills[1].price_quantity, pack_price_quantity(100U, 1U));
    ASSERT_EQ(snapshot.bids.size(), 1U);
    EXPECT_EQ(snapshot.bids.front().price, 100U);
    EXPECT_EQ(snapshot.bids.front().quantity, 9U);
    EXPECT_EQ(snapshot.bids.front().order_count, 1U);
    EXPECT_EQ(first_cancel.status, CancelStatus::not_found);
    EXPECT_EQ(second_cancel.status, CancelStatus::cancelled);
    EXPECT_EQ(second_cancel.cancelled_quantity, 9U);
    EXPECT_EQ(unknown_cancel.status, CancelStatus::not_found);
}

TEST(OrderBookTest, BetterPriceMatchesFirst) {
    OrderBook book{make_instrument()};

    const auto worse = book.submit(OrderRequest{Side::buy, OrderType::limit, TimeInForce::gtc, 99U, 5U});
    const auto better = book.submit(OrderRequest{Side::buy, OrderType::limit, TimeInForce::gtc, 100U, 5U});
    const auto taker = book.submit(OrderRequest{Side::sell, OrderType::market, TimeInForce::ioc, 0U, 7U});
    const auto snapshot = book.snapshot(10U);
    const auto worse_cancel = book.cancel(worse.order_id);
    const auto better_cancel = book.cancel(better.order_id);

    EXPECT_TRUE(worse.accepted);
    EXPECT_TRUE(better.accepted);
    EXPECT_TRUE(taker.accepted);
    EXPECT_EQ(taker.matched_quantity, 7U);
    ASSERT_EQ(taker.fills.size(), 2U);
    EXPECT_EQ(taker.fills[0].maker_order_id, better.order_id);
    EXPECT_EQ(taker.fills[0].price_quantity, pack_price_quantity(100U, 5U));
    EXPECT_EQ(taker.fills[1].maker_order_id, worse.order_id);
    EXPECT_EQ(taker.fills[1].price_quantity, pack_price_quantity(99U, 2U));
    ASSERT_EQ(snapshot.bids.size(), 1U);
    EXPECT_EQ(snapshot.bids.front().price, 99U);
    EXPECT_EQ(snapshot.bids.front().quantity, 3U);
    EXPECT_EQ(snapshot.bids.front().order_count, 1U);
    EXPECT_EQ(worse_cancel.status, CancelStatus::cancelled);
    EXPECT_EQ(worse_cancel.cancelled_quantity, 3U);
    EXPECT_EQ(better_cancel.status, CancelStatus::not_found);
}

TEST(OrderBookTest, CrossingLimitLeavesResidualGtc) {
    OrderBook book{make_instrument()};

    const auto ask_low = book.submit(OrderRequest{Side::sell, OrderType::limit, TimeInForce::gtc, 100U, 2U});
    const auto ask_high = book.submit(OrderRequest{Side::sell, OrderType::limit, TimeInForce::gtc, 101U, 1U});
    const auto buy = book.submit(OrderRequest{Side::buy, OrderType::limit, TimeInForce::gtc, 101U, 5U});
    const auto snapshot = book.snapshot(10U);

    EXPECT_TRUE(ask_low.accepted);
    EXPECT_TRUE(ask_high.accepted);
    EXPECT_TRUE(buy.accepted);
    EXPECT_TRUE(buy.resting);
    EXPECT_EQ(buy.matched_quantity, 3U);
    EXPECT_EQ(buy.resting_quantity, 2U);
    ASSERT_EQ(buy.fills.size(), 2U);
    EXPECT_EQ(buy.fills[0].maker_order_id, ask_low.order_id);
    EXPECT_EQ(buy.fills[0].price_quantity, pack_price_quantity(100U, 2U));
    EXPECT_EQ(buy.fills[1].maker_order_id, ask_high.order_id);
    EXPECT_EQ(buy.fills[1].price_quantity, pack_price_quantity(101U, 1U));
    ASSERT_EQ(snapshot.bids.size(), 1U);
    EXPECT_EQ(snapshot.bids.front().price, 101U);
    EXPECT_EQ(snapshot.bids.front().quantity, 2U);
    EXPECT_EQ(snapshot.bids.front().order_count, 1U);
    EXPECT_TRUE(snapshot.asks.empty());
}

TEST(OrderBookTest, SnapshotAggregatesDepthAndOrderCount) {
    OrderBook book{make_instrument()};

    const auto bid_a = book.submit(OrderRequest{Side::buy, OrderType::limit, TimeInForce::gtc, 105U, 4U});
    const auto bid_b = book.submit(OrderRequest{Side::buy, OrderType::limit, TimeInForce::gtc, 105U, 6U});
    const auto bid_c = book.submit(OrderRequest{Side::buy, OrderType::limit, TimeInForce::gtc, 104U, 3U});
    const auto ask_a = book.submit(OrderRequest{Side::sell, OrderType::limit, TimeInForce::gtc, 110U, 2U});
    const auto ask_b = book.submit(OrderRequest{Side::sell, OrderType::limit, TimeInForce::gtc, 111U, 5U});
    const auto depth1 = book.snapshot(1U);
    const auto depth2 = book.snapshot(2U);

    EXPECT_TRUE(bid_a.accepted);
    EXPECT_TRUE(bid_b.accepted);
    EXPECT_TRUE(bid_c.accepted);
    EXPECT_TRUE(ask_a.accepted);
    EXPECT_TRUE(ask_b.accepted);
    ASSERT_EQ(depth1.bids.size(), 1U);
    ASSERT_EQ(depth1.asks.size(), 1U);
    EXPECT_EQ(depth1.bids.front().price, 105U);
    EXPECT_EQ(depth1.bids.front().quantity, 10U);
    EXPECT_EQ(depth1.bids.front().order_count, 2U);
    EXPECT_EQ(depth1.asks.front().price, 110U);
    EXPECT_EQ(depth1.asks.front().quantity, 2U);
    EXPECT_EQ(depth1.asks.front().order_count, 1U);
    ASSERT_EQ(depth2.bids.size(), 2U);
    ASSERT_EQ(depth2.asks.size(), 2U);
    EXPECT_EQ(depth2.bids[1].price, 104U);
    EXPECT_EQ(depth2.bids[1].quantity, 3U);
    EXPECT_EQ(depth2.asks[1].price, 111U);
    EXPECT_EQ(depth2.asks[1].quantity, 5U);
}

TEST(OrderBookTest, CancelRestingOrderRemovesActiveLiquidity) {
    OrderBook book{make_instrument()};

    const auto submit = book.submit(OrderRequest{Side::sell, OrderType::limit, TimeInForce::gtc, 150U, 12U});
    const auto before_cancel = book.sequence();
    const auto cancel = book.cancel(submit.order_id);
    const auto cancel_again = book.cancel(submit.order_id);
    const auto snapshot = book.snapshot(10U);

    EXPECT_TRUE(submit.accepted);
    EXPECT_TRUE(submit.resting);
    EXPECT_EQ(cancel.status, CancelStatus::cancelled);
    EXPECT_EQ(cancel.cancelled_quantity, 12U);
    EXPECT_EQ(book.sequence(), before_cancel + 1U);
    EXPECT_EQ(cancel_again.status, CancelStatus::not_found);
    EXPECT_TRUE(snapshot.bids.empty());
    EXPECT_TRUE(snapshot.asks.empty());
}

TEST(OrderBookTest, FokRejectsWithoutStateChange) {
    OrderBook book{make_instrument()};

    const auto resting = book.submit(OrderRequest{Side::sell, OrderType::limit, TimeInForce::gtc, 110U, 4U});
    const auto before = book.sequence();
    const auto fok = book.submit(OrderRequest{Side::buy, OrderType::limit, TimeInForce::fok, 120U, 5U});
    const auto snapshot = book.snapshot(10U);

    EXPECT_TRUE(resting.accepted);
    EXPECT_FALSE(fok.accepted);
    EXPECT_EQ(fok.reject_reason, SubmitRejectReason::insufficient_liquidity);
    EXPECT_EQ(book.sequence(), before);
    ASSERT_EQ(snapshot.asks.size(), 1U);
    EXPECT_EQ(snapshot.asks.front().price, 110U);
    EXPECT_EQ(snapshot.asks.front().quantity, 4U);
}

}  // namespace
