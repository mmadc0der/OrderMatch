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
