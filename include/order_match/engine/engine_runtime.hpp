#pragma once

#include "order_match/engine/engine_runner.hpp"
#include "order_match/engine/event_bus.hpp"
#include "order_match/engine/task.hpp"

#include <atomic>
#include <future>
#include <memory>
#include <thread>
#include <vector>

namespace order_match::engine {

class EngineRuntime {
public:
    struct InboundJob {
        InboundEvent event{};
        std::promise<ExecutionReport> promise{};
    };

    using InboundJobPtr = std::shared_ptr<InboundJob>;

    explicit EngineRuntime(std::size_t inbound_capacity = 1024U,
                           std::size_t core_capacity = 1024U,
                           std::size_t aux_capacity = 1024U,
                           std::size_t aux_workers = 1U);
    EngineRuntime(const EngineRuntime&) = delete;
    EngineRuntime& operator=(const EngineRuntime&) = delete;
    ~EngineRuntime();

    [[nodiscard]] std::future<ExecutionReport> submit(const InboundEvent& event);
    [[nodiscard]] bool post_core_aware(EngineTask task);
    [[nodiscard]] bool post_core_agnostic(EngineTask task);
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;

private:
    struct QueuedTask {
        TaskKind kind{TaskKind::core_aware};
        std::function<void()> work{};
    };

    static ExecutionReport make_overloaded_report(const InboundEvent& event) noexcept;
    static std::future<ExecutionReport> make_ready_future(ExecutionReport report);

    void start_workers(std::size_t aux_workers);
    void coordinator_loop();
    void core_loop();
    void aux_loop();

    EventBus<InboundJobPtr> inbound_bus_;
    EventBus<QueuedTask> core_bus_;
    EventBus<QueuedTask> aux_bus_;
    std::thread coordinator_thread_{};
    std::thread core_thread_{};
    std::vector<std::thread> aux_threads_{};
    std::atomic<bool> running_{false};
    EngineRunner runner_{};
};

}  // namespace order_match::engine
