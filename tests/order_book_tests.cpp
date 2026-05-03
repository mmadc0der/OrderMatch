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

TEST(OrderBookTest, RejectedSubmitDoesNotMutate) {
    OrderBook book{make_instrument()};

    const auto before = book.sequence();
    const auto result = book.submit(OrderRequest{Side::buy, OrderType::limit, TimeInForce::gtc, 0U, 10U});
    const auto snapshot = book.snapshot(10U);

    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(book.sequence(), before);
    EXPECT_EQ(snapshot.sequence, before);
    EXPECT_TRUE(snapshot.bids.empty());
    EXPECT_TRUE(snapshot.asks.empty());
}

TEST(OrderBookTest, RestingLimitOrderIsVisible) {
    OrderBook book{make_instrument()};

    const auto result = book.submit(OrderRequest{Side::buy, OrderType::limit, TimeInForce::gtc, 100U, 10U});
    const auto snapshot = book.snapshot(10U);

    EXPECT_TRUE(result.accepted);
    EXPECT_TRUE(result.resting);
    EXPECT_EQ(result.matched_quantity, 0U);
    EXPECT_EQ(result.resting_quantity, 10U);
    EXPECT_EQ(snapshot.sequence, book.sequence());
    ASSERT_EQ(snapshot.bids.size(), 1U);
    EXPECT_EQ(snapshot.bids.front().price, 100U);
    EXPECT_EQ(snapshot.bids.front().quantity, 10U);
    EXPECT_EQ(snapshot.bids.front().order_count, 1U);
    EXPECT_TRUE(snapshot.asks.empty());
}

TEST(OrderBookTest, FifoWithinPriceLevel) {
    OrderBook book{make_instrument()};

    const auto first = book.submit(OrderRequest{Side::buy, OrderType::limit, TimeInForce::gtc, 100U, 7U});
    const auto second = book.submit(OrderRequest{Side::buy, OrderType::limit, TimeInForce::gtc, 100U, 10U});
    const auto taker = book.submit(OrderRequest{Side::sell, OrderType::market, TimeInForce::ioc, 0U, 8U});
    const auto snapshot_before_cancel = book.snapshot(10U);
    const auto first_cancel = book.cancel(first.order_id);
    const auto second_cancel = book.cancel(second.order_id);
    const auto snapshot_after_cancel = book.snapshot(10U);

    EXPECT_TRUE(first.accepted);
    EXPECT_TRUE(second.accepted);
    EXPECT_TRUE(taker.accepted);
    EXPECT_EQ(taker.matched_quantity, 8U);
    EXPECT_EQ(taker.resting_quantity, 0U);
    ASSERT_EQ(snapshot_before_cancel.bids.size(), 1U);
    EXPECT_EQ(snapshot_before_cancel.bids.front().price, 100U);
    EXPECT_EQ(snapshot_before_cancel.bids.front().quantity, 9U);
    EXPECT_EQ(snapshot_before_cancel.bids.front().order_count, 1U);
    EXPECT_EQ(first_cancel.status, CancelStatus::already_terminal);
    EXPECT_EQ(second_cancel.status, CancelStatus::cancelled);
    EXPECT_EQ(second_cancel.cancelled_quantity, 9U);
    EXPECT_TRUE(snapshot_after_cancel.bids.empty());
}

TEST(OrderBookTest, BetterPriceMatchesFirst) {
    OrderBook book{make_instrument()};

    const auto worse = book.submit(OrderRequest{Side::buy, OrderType::limit, TimeInForce::gtc, 99U, 5U});
    const auto better = book.submit(OrderRequest{Side::buy, OrderType::limit, TimeInForce::gtc, 100U, 5U});
    const auto taker = book.submit(OrderRequest{Side::sell, OrderType::market, TimeInForce::ioc, 0U, 7U});
    const auto snapshot_before_cancel = book.snapshot(10U);
    const auto worse_cancel = book.cancel(worse.order_id);
    const auto better_cancel = book.cancel(better.order_id);
    const auto snapshot_after_cancel = book.snapshot(10U);

    EXPECT_TRUE(worse.accepted);
    EXPECT_TRUE(better.accepted);
    EXPECT_TRUE(taker.accepted);
    EXPECT_EQ(taker.matched_quantity, 7U);
    ASSERT_EQ(snapshot_before_cancel.bids.size(), 1U);
    EXPECT_EQ(snapshot_before_cancel.bids.front().price, 99U);
    EXPECT_EQ(snapshot_before_cancel.bids.front().quantity, 3U);
    EXPECT_EQ(snapshot_before_cancel.bids.front().order_count, 1U);
    EXPECT_EQ(worse_cancel.status, CancelStatus::cancelled);
    EXPECT_EQ(worse_cancel.cancelled_quantity, 3U);
    EXPECT_EQ(better_cancel.status, CancelStatus::already_terminal);
    EXPECT_TRUE(snapshot_after_cancel.bids.empty());
}

TEST(OrderBookTest, CancelRestingOrder) {
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
    EXPECT_EQ(cancel_again.status, CancelStatus::already_terminal);
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
    EXPECT_EQ(book.sequence(), before);
    ASSERT_EQ(snapshot.asks.size(), 1U);
    EXPECT_EQ(snapshot.asks.front().price, 110U);
    EXPECT_EQ(snapshot.asks.front().quantity, 4U);
}

}  // namespace
