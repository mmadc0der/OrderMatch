#include "order_match/engine/engine_runtime.hpp"

#include <utility>

namespace order_match::engine {

namespace {

[[nodiscard]] constexpr bool has_flag(const EventFlags flags, const EventFlags mask) noexcept {
    return (flags & mask) != 0U;
}

[[nodiscard]] constexpr bool has_only_known_flags(const EventFlags flags) noexcept {
    constexpr EventFlags known_flags = event_flags::side_sell | event_flags::kind_market | event_flags::tif_ioc |
                                       event_flags::tif_fok;
    return (flags & static_cast<EventFlags>(~known_flags)) == 0U;
}

[[nodiscard]] ResultCode map_submit_reject(const core::SubmitRejectReason reject_reason) noexcept {
    switch (reject_reason) {
    case core::SubmitRejectReason::none:
        return result_code::ok;
    case core::SubmitRejectReason::invalid_request:
        return result_code::malformed_request;
    case core::SubmitRejectReason::insufficient_liquidity:
        return result_code::insufficient_liquidity;
    }

    return result_code::rejected;
}

[[nodiscard]] bool decode_request(const InboundEvent& event, core::OrderRequest& request, ResultCode& result) noexcept {
    if (event.type != event_type::submit_order) {
        result = result_code::malformed_request;
        return false;
    }

    if (!has_only_known_flags(event.flags)) {
        result = result_code::invalid_order_type;
        return false;
    }

    const bool is_market = has_flag(event.flags, event_flags::kind_market);
    const bool tif_ioc = has_flag(event.flags, event_flags::tif_ioc);
    const bool tif_fok = has_flag(event.flags, event_flags::tif_fok);

    if (tif_ioc && tif_fok) {
        result = result_code::invalid_time_in_force;
        return false;
    }

    request.side = has_flag(event.flags, event_flags::side_sell) ? core::Side::sell : core::Side::buy;
    request.type = is_market ? core::OrderType::market : core::OrderType::limit;
    request.time_in_force = tif_fok ? core::TimeInForce::fok : (tif_ioc ? core::TimeInForce::ioc : core::TimeInForce::gtc);
    request.price = core::unpack_price(event.price_quantity);
    request.quantity = core::unpack_quantity(event.price_quantity);

    if (request.quantity == 0U) {
        result = result_code::invalid_quantity;
        return false;
    }

    if (!is_market && request.price == 0U) {
        result = result_code::invalid_price;
        return false;
    }

    result = result_code::ok;
    return true;
}

}  // namespace

EngineRuntime::EngineRuntime(const std::size_t inbound_capacity,
                             const std::size_t core_capacity,
                             const std::size_t aux_capacity,
                             const std::size_t aux_workers)
    : inbound_bus_(inbound_capacity),
      core_bus_(core_capacity),
      aux_bus_(aux_capacity) {
    start_workers(aux_workers == 0U ? 1U : aux_workers);
}

EngineRuntime::~EngineRuntime() {
    stop();
}

std::future<ExecutionReport> EngineRuntime::make_ready_future(ExecutionReport report) {
    std::promise<ExecutionReport> promise{};
    auto future = promise.get_future();
    promise.set_value(std::move(report));
    return future;
}

ExecutionReport EngineRuntime::make_overloaded_report(const InboundEvent& event) noexcept {
    ExecutionReport report{};
    report.request_id = event.request_id;
    report.order_id = event.order_id;
    report.result = result_code::overloaded;
    return report;
}

void EngineRuntime::start_workers(const std::size_t aux_workers) {
    running_.store(true, std::memory_order_release);
    coordinator_thread_ = std::thread(&EngineRuntime::coordinator_loop, this);
    core_thread_ = std::thread(&EngineRuntime::core_loop, this);
    aux_threads_.reserve(aux_workers);
    for (std::size_t index = 0U; index < aux_workers; ++index) {
        aux_threads_.emplace_back(&EngineRuntime::aux_loop, this);
    }
}

std::future<ExecutionReport> EngineRuntime::submit(const InboundEvent& event) {
    auto job = std::make_shared<InboundJob>();
    job->event = event;
    auto future = job->promise.get_future();
    if (!running_.load(std::memory_order_acquire) || !inbound_bus_.try_push(std::move(job))) {
        return make_ready_future(make_overloaded_report(event));
    }

    return future;
}

bool EngineRuntime::post_core_aware(EngineTask task) {
    if (!running_.load(std::memory_order_acquire)) {
        return false;
    }

    return core_bus_.try_push(QueuedTask{TaskKind::core_aware, std::move(task.work)});
}

bool EngineRuntime::post_core_agnostic(EngineTask task) {
    if (!running_.load(std::memory_order_acquire)) {
        return false;
    }

    return aux_bus_.try_push(QueuedTask{TaskKind::core_agnostic, std::move(task.work)});
}

void EngineRuntime::stop() noexcept {
    const bool was_running = running_.exchange(false, std::memory_order_acq_rel);
    if (!was_running) {
        return;
    }

    inbound_bus_.close();
    core_bus_.close();
    aux_bus_.close();

    if (coordinator_thread_.joinable()) {
        coordinator_thread_.join();
    }

    if (core_thread_.joinable()) {
        core_thread_.join();
    }

    for (auto& thread : aux_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    aux_threads_.clear();
}

bool EngineRuntime::running() const noexcept {
    return running_.load(std::memory_order_acquire);
}

BookViewSnapshot EngineRuntime::bake_book_view(const core::DepthLimit depth) const {
    return runner_.bake_book_view(depth);
}

TopOfBookView EngineRuntime::top_of_book() const noexcept {
    return runner_.top_of_book();
}

std::size_t EngineRuntime::inbound_capacity() const noexcept {
    return inbound_bus_.capacity();
}

std::size_t EngineRuntime::inbound_available() const noexcept {
    return inbound_bus_.available_capacity();
}

void EngineRuntime::coordinator_loop() {
    while (running_.load(std::memory_order_acquire)) {
        InboundJobPtr job{};
        if (!inbound_bus_.wait_pop(job)) {
            break;
        }

        auto task_job = job;
        const bool queued = core_bus_.try_push(QueuedTask{
            TaskKind::core_aware,
            [this, task_job]() mutable {
                ExecutionReport report = runner_.process(task_job->event);
                task_job->promise.set_value(std::move(report));
            },
        });

        if (!queued) {
            task_job->promise.set_value(make_overloaded_report(task_job->event));
        }
    }
}

void EngineRuntime::core_loop() {
    while (running_.load(std::memory_order_acquire)) {
        QueuedTask task{};
        if (!core_bus_.wait_pop(task)) {
            break;
        }

        if (task.work) {
            task.work();
        }
    }
}

void EngineRuntime::aux_loop() {
    while (running_.load(std::memory_order_acquire)) {
        QueuedTask task{};
        if (!aux_bus_.wait_pop(task)) {
            break;
        }

        if (task.work) {
            task.work();
        }
    }
}

}  // namespace order_match::engine
