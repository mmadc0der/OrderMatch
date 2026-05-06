#include "order_match/engine/engine_runner.hpp"

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

[[nodiscard]] ResultCode decode_submit(const InboundEvent& event, core::OrderRequest& request) noexcept {
    if (event.type != event_type::submit_order) {
        return result_code::malformed_request;
    }

    if (!has_only_known_flags(event.flags)) {
        return result_code::invalid_order_type;
    }

    const bool is_market = has_flag(event.flags, event_flags::kind_market);
    const bool tif_ioc = has_flag(event.flags, event_flags::tif_ioc);
    const bool tif_fok = has_flag(event.flags, event_flags::tif_fok);

    if (tif_ioc && tif_fok) {
        return result_code::invalid_time_in_force;
    }

    request.side = has_flag(event.flags, event_flags::side_sell) ? core::Side::sell : core::Side::buy;
    request.type = is_market ? core::OrderType::market : core::OrderType::limit;
    request.time_in_force = tif_fok ? core::TimeInForce::fok : (tif_ioc ? core::TimeInForce::ioc : core::TimeInForce::gtc);
    request.price = core::unpack_price(event.price_quantity);
    request.quantity = core::unpack_quantity(event.price_quantity);

    if (request.quantity == 0U) {
        return result_code::invalid_quantity;
    }

    if (!is_market && request.price == 0U) {
        return result_code::invalid_price;
    }

    return result_code::ok;
}

[[nodiscard]] ExecutionFill to_execution_fill(const core::Trade& trade) noexcept {
    return ExecutionFill{
        trade.taker_order_id,
        trade.maker_order_id,
        trade.price_quantity,
        trade.sequence,
    };
}

}  // namespace

ExecutionReport EngineRunner::process(const InboundEvent& event) {
    ExecutionReport report{};
    report.request_id = event.request_id;

    if (event.type == event_type::cancel_order) {
        const auto cancel = engine_.book().cancel(event.order_id);
        report.order_id = cancel.order_id;
        report.sequence = cancel.sequence;
        report.result = cancel.status == core::CancelStatus::cancelled ? result_code::ok : result_code::not_found;
        report.status = cancel.status == core::CancelStatus::cancelled ? ExecutionStatus::cancelled : ExecutionStatus::none;
        report.leaves_quantity = 0U;
        report.cumulative_quantity = 0U;
        if (cancel.status == core::CancelStatus::cancelled) {
            book_view_.publish(engine_.book().snapshot(max_book_depth));
        }
        return report;
    }

    core::OrderRequest request{};
    const ResultCode decode_result = decode_submit(event, request);
    if (decode_result != result_code::ok) {
        report.order_id = event.order_id;
        report.result = decode_result;
        return report;
    }

    const auto submit = engine_.book().submit(request);
    report.order_id = submit.order_id;
    report.sequence = submit.sequence;
    report.result = submit.accepted ? result_code::ok : map_submit_reject(submit.reject_reason);
    report.status = submit.accepted ? (submit.resting ? ExecutionStatus::resting : ExecutionStatus::filled)
                                    : ExecutionStatus::none;
    report.cumulative_quantity = submit.matched_quantity;
    report.leaves_quantity = submit.resting_quantity;
    report.fills.reserve(submit.fills.size());
    for (const auto& fill : submit.fills) {
        report.fills.push_back(to_execution_fill(fill));
    }
    if (submit.accepted) {
        book_view_.publish(engine_.book().snapshot(max_book_depth));
    }

    return report;
}

BookViewSnapshot EngineRunner::bake_book_view(const core::DepthLimit depth) const {
    return book_view_.bake(depth);
}

TopOfBookView EngineRunner::top_of_book() const noexcept {
    return book_view_.top();
}

}  // namespace order_match::engine
