#include "order_match/engine/engine_runtime.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>

namespace {

using namespace order_match::core;
using namespace order_match::engine;
using namespace std::chrono_literals;

[[nodiscard]] InboundEvent make_submit_event(const RequestId request_id,
                                             const OrderId order_id,
                                             const EventFlags flags,
                                             const PriceTicks price,
                                             const QuantityUnits quantity) {
    InboundEvent event{};
    event.request_id = request_id;
    event.order_id = order_id;
    event.flags = flags;
    event.price_quantity = pack_price_quantity(price, quantity);
    event.type = event_type::submit_order;
    return event;
}

[[nodiscard]] InboundEvent make_cancel_event(const RequestId request_id, const OrderId order_id) {
    InboundEvent event{};
    event.request_id = request_id;
    event.order_id = order_id;
    event.type = event_type::cancel_order;
    return event;
}

TEST(EngineEventBusTest, MaintainsFifoAndCapacity) {
    EventBus<int> bus{2U};

    EXPECT_TRUE(bus.try_push(1));
    EXPECT_TRUE(bus.try_push(2));
    EXPECT_FALSE(bus.try_push(3));

    int value{};
    EXPECT_TRUE(bus.try_pop(value));
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(bus.try_pop(value));
    EXPECT_EQ(value, 2);
    EXPECT_FALSE(bus.try_pop(value));
}

TEST(EngineRunnerTest, ProcessesSubmitAndCancel) {
    EngineRunner runner{};

    const auto resting = runner.process(make_submit_event(11U, 0U, 0U, 100U, 5U));
    const auto cancel = runner.process(make_cancel_event(12U, resting.order_id));

    EXPECT_EQ(resting.result, result_code::ok);
    EXPECT_EQ(resting.order_id, 1U);
    EXPECT_EQ(resting.sequence, 1U);
    EXPECT_EQ(resting.cumulative_quantity, 0U);
    EXPECT_EQ(resting.leaves_quantity, 5U);
    EXPECT_TRUE(resting.fills.empty());

    EXPECT_EQ(cancel.result, result_code::ok);
    EXPECT_EQ(cancel.order_id, resting.order_id);
    EXPECT_EQ(cancel.sequence, 2U);
    EXPECT_EQ(cancel.leaves_quantity, 0U);
}

TEST(EngineRunnerTest, PublishesBookViewAfterAcceptedSubmit) {
    EngineRunner runner{};

    const auto submit = runner.process(make_submit_event(13U, 0U, static_cast<EventFlags>(event_flags::side_sell), 101U, 5U));
    const auto top = runner.top_of_book();
    const auto snapshot = runner.bake_book_view(10U);

    EXPECT_EQ(submit.result, result_code::ok);
    EXPECT_EQ(top.sequence, submit.sequence);
    EXPECT_EQ(top.best_bid, 0U);
    EXPECT_EQ(top.best_ask, pack_price_quantity(101U, 5U));
    EXPECT_EQ(snapshot.sequence, submit.sequence);
    ASSERT_EQ(snapshot.asks.size(), 1U);
    EXPECT_EQ(snapshot.asks.front().price_quantity, pack_price_quantity(101U, 5U));
    EXPECT_EQ(snapshot.asks.front().order_count, 1U);
    EXPECT_TRUE(snapshot.bids.empty());
}

TEST(EngineRunnerTest, ClipsPublishedBookViewDepth) {
    EngineRunner runner{};

    const auto bid_1 = runner.process(make_submit_event(14U, 0U, 0U, 100U, 4U));
    const auto bid_2 = runner.process(make_submit_event(15U, 0U, 0U, 99U, 3U));
    const auto ask_1 = runner.process(make_submit_event(16U, 0U, static_cast<EventFlags>(event_flags::side_sell), 110U, 2U));
    const auto ask_2 = runner.process(make_submit_event(17U, 0U, static_cast<EventFlags>(event_flags::side_sell), 111U, 1U));
    const auto depth_1 = runner.bake_book_view(1U);
    const auto depth_0 = runner.bake_book_view(0U);

    EXPECT_EQ(bid_1.result, result_code::ok);
    EXPECT_EQ(bid_2.result, result_code::ok);
    EXPECT_EQ(ask_1.result, result_code::ok);
    EXPECT_EQ(ask_2.result, result_code::ok);
    ASSERT_EQ(depth_1.bids.size(), 1U);
    ASSERT_EQ(depth_1.asks.size(), 1U);
    EXPECT_EQ(depth_1.bids.front().price_quantity, pack_price_quantity(100U, 4U));
    EXPECT_EQ(depth_1.asks.front().price_quantity, pack_price_quantity(110U, 2U));
    EXPECT_EQ(depth_0.sequence, depth_1.sequence);
    EXPECT_TRUE(depth_0.bids.empty());
    EXPECT_TRUE(depth_0.asks.empty());
}

TEST(EngineRunnerTest, RejectedMutationsDoNotChangePublishedView) {
    EngineRunner runner{};

    const auto accepted = runner.process(make_submit_event(18U, 0U, 0U, 100U, 5U));
    const auto before = runner.bake_book_view(10U);
    const auto rejected = runner.process(make_submit_event(19U, 0U, 0U, 100U, 0U));
    const auto after = runner.bake_book_view(10U);
    const auto missing_cancel = runner.process(make_cancel_event(20U, 999U));
    const auto after_cancel = runner.bake_book_view(10U);

    EXPECT_EQ(accepted.result, result_code::ok);
    EXPECT_EQ(rejected.result, result_code::invalid_quantity);
    EXPECT_EQ(before.sequence, after.sequence);
    EXPECT_EQ(after.sequence, after_cancel.sequence);
    ASSERT_EQ(before.bids.size(), after.bids.size());
    ASSERT_EQ(after.bids.size(), after_cancel.bids.size());
    ASSERT_EQ(before.bids.size(), 1U);
    EXPECT_EQ(before.bids.front().price_quantity, pack_price_quantity(100U, 5U));
    EXPECT_EQ(after.bids.front().price_quantity, pack_price_quantity(100U, 5U));
    EXPECT_EQ(after_cancel.bids.front().price_quantity, pack_price_quantity(100U, 5U));
    EXPECT_EQ(missing_cancel.result, result_code::not_found);
}

TEST(EngineRuntimeTest, ProcessesInboundEventsThroughCoreLane) {
    EngineRuntime runtime{16U, 16U, 16U, 1U};

    const auto resting = runtime.submit(make_submit_event(
        21U,
        0U,
        static_cast<EventFlags>(event_flags::side_sell),
        100U,
        5U)).get();
    const auto taker = runtime.submit(make_submit_event(
        22U,
        0U,
        static_cast<EventFlags>(event_flags::kind_market | event_flags::tif_ioc),
        0U,
        3U)).get();
    const auto cancel = runtime.submit(make_cancel_event(23U, resting.order_id)).get();

    EXPECT_EQ(resting.result, result_code::ok);
    EXPECT_EQ(resting.order_id, 1U);
    EXPECT_EQ(resting.sequence, 1U);
    EXPECT_EQ(taker.result, result_code::ok);
    EXPECT_EQ(taker.order_id, 2U);
    EXPECT_EQ(taker.sequence, 2U);
    EXPECT_EQ(taker.cumulative_quantity, 3U);
    EXPECT_EQ(taker.leaves_quantity, 0U);
    ASSERT_EQ(taker.fills.size(), 1U);
    EXPECT_EQ(taker.fills.front().maker_order_id, resting.order_id);
    EXPECT_EQ(taker.fills.front().taker_order_id, taker.order_id);
    EXPECT_EQ(taker.fills.front().price_quantity, pack_price_quantity(100U, 3U));

    EXPECT_EQ(cancel.result, result_code::ok);
    EXPECT_EQ(cancel.sequence, 3U);
}

TEST(EngineRuntimeTest, SerializesCoreAwareTasks) {
    EngineRuntime runtime{8U, 8U, 8U, 1U};
    std::atomic<int> active_core{0};
    std::atomic<int> max_core{0};
    std::mutex mutex{};
    std::condition_variable cv{};
    int completed{};

    auto note_completion = [&]() {
        std::scoped_lock lock(mutex);
        ++completed;
        cv.notify_one();
    };

    auto make_task = [&]() {
        return EngineTask{
            TaskKind::core_aware,
            [&]() {
                const int active = active_core.fetch_add(1) + 1;
                int current = max_core.load();
                while (active > current && !max_core.compare_exchange_weak(current, active)) {
                }
                std::this_thread::sleep_for(50ms);
                active_core.fetch_sub(1);
                note_completion();
            },
        };
    };

    EXPECT_TRUE(runtime.post_core_aware(make_task()));
    EXPECT_TRUE(runtime.post_core_aware(make_task()));

    std::unique_lock lock(mutex);
    cv.wait_for(lock, 2s, [&]() noexcept { return completed == 2; });
    EXPECT_EQ(max_core.load(), 1);
}

TEST(EngineRuntimeTest, AllowsAuxTasksToOverlapCoreWork) {
    EngineRuntime runtime{8U, 8U, 8U, 1U};
    std::atomic<bool> core_running{false};
    std::atomic<bool> overlap_detected{false};
    std::mutex mutex{};
    std::condition_variable cv{};
    bool aux_done{};
    std::promise<void> core_started{};
    auto core_started_future = core_started.get_future();

    EXPECT_TRUE(runtime.post_core_aware(EngineTask{
        TaskKind::core_aware,
        [&]() {
            core_running.store(true, std::memory_order_release);
            core_started.set_value();
            std::this_thread::sleep_for(150ms);
            core_running.store(false, std::memory_order_release);
        },
    }));

    ASSERT_EQ(core_started_future.wait_for(2s), std::future_status::ready);
    EXPECT_TRUE(runtime.post_core_agnostic(EngineTask{
        TaskKind::core_agnostic,
        [&]() {
            if (core_running.load(std::memory_order_acquire)) {
                overlap_detected.store(true, std::memory_order_release);
            }

            {
                std::scoped_lock lock(mutex);
                aux_done = true;
            }
            cv.notify_one();
        },
    }));

    std::unique_lock lock(mutex);
    cv.wait_for(lock, 2s, [&]() noexcept { return aux_done; });
    EXPECT_TRUE(overlap_detected.load(std::memory_order_acquire));
}

}  // namespace
