#pragma once

#include "order_match/core/instrument.hpp"
#include "order_match/core/order.hpp"
#include "order_match/engine/engine_runtime.hpp"
#include "order_match/http/codec.hpp"
#include "order_match/server/routes.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace order_match::server {

struct HttpRequest {
    core::RequestId request_id{};
    std::string method{};
    std::string target{};
    std::string body{};
};

struct HttpResponse {
    int status_code{};
    std::string body{};
};

class HttpPipeline {
public:
    HttpPipeline(engine::EngineRuntime& runtime, const core::InstrumentConfig& instrument) noexcept;

    [[nodiscard]] HttpResponse handle(const HttpRequest& request);

private:
    [[nodiscard]] static int status_for_result(engine::ResultCode result) noexcept;
    [[nodiscard]] static std::string message_for_result(engine::ResultCode result);

    engine::EngineRuntime& runtime_;
    const core::InstrumentConfig& instrument_;
};

inline HttpPipeline::HttpPipeline(engine::EngineRuntime& runtime, const core::InstrumentConfig& instrument) noexcept
    : runtime_(runtime), instrument_(instrument) {}

inline int HttpPipeline::status_for_result(const engine::ResultCode result) noexcept {
    switch (result) {
    case engine::result_code::ok:
        return 200;
    case engine::result_code::not_found:
        return 404;
    case engine::result_code::insufficient_liquidity:
    case engine::result_code::already_terminal:
        return 409;
    case engine::result_code::overloaded:
        return 503;
    case engine::result_code::malformed_request:
    case engine::result_code::invalid_price:
    case engine::result_code::invalid_quantity:
    case engine::result_code::invalid_order_type:
    case engine::result_code::invalid_time_in_force:
    case engine::result_code::rejected:
    default:
        return 400;
    }
}

inline std::string HttpPipeline::message_for_result(const engine::ResultCode result) {
    switch (result) {
    case engine::result_code::ok:
        return "ok";
    case engine::result_code::not_found:
        return "order not found";
    case engine::result_code::insufficient_liquidity:
        return "insufficient liquidity";
    case engine::result_code::overloaded:
        return "engine overloaded";
    case engine::result_code::invalid_price:
        return "invalid price";
    case engine::result_code::invalid_quantity:
        return "invalid quantity";
    case engine::result_code::invalid_order_type:
        return "invalid order type";
    case engine::result_code::invalid_time_in_force:
        return "invalid time in force";
    case engine::result_code::already_terminal:
        return "already terminal";
    case engine::result_code::malformed_request:
        return "malformed request";
    case engine::result_code::rejected:
    default:
        return "rejected";
    }
}

inline HttpResponse HttpPipeline::handle(const HttpRequest& request) {
    if (Routes::is_health(request.method, request.target)) {
        return HttpResponse{
            200,
            http::encode_health_response(runtime_.running(),
                                         runtime_.running() ? "running" : "stopped",
                                         runtime_.inbound_capacity(),
                                         runtime_.inbound_available(),
                                         runtime_.top_of_book().sequence),
        };
    }

    if (Routes::is_book(request.method, request.target)) {
        const auto depth = Routes::parse_book_depth(request.target);
        if (!depth.has_value()) {
            return HttpResponse{
                400,
                http::encode_error_response(request.request_id,
                                            engine::result_code::malformed_request,
                                            "invalid book depth"),
            };
        }

        return HttpResponse{
            200,
            http::encode_book_snapshot(runtime_.bake_book_view(*depth), *depth, instrument_),
        };
    }

    if (Routes::is_order_request(request.method, request.target)) {
        const auto decoded = http::decode_request(request.method, request.target, request.body, instrument_);
        if (decoded.result != engine::result_code::ok) {
            return HttpResponse{
                status_for_result(decoded.result),
                http::encode_error_response(request.request_id, decoded.result, message_for_result(decoded.result)),
            };
        }

        auto event = decoded.event;
        event.request_id = request.request_id;
        const auto report = runtime_.submit(event).get();
        if (report.result != engine::result_code::ok) {
            return HttpResponse{
                status_for_result(report.result),
                http::encode_error_response(request.request_id, report.result, message_for_result(report.result)),
            };
        }

        return HttpResponse{
            200,
            http::encode_execution_report(report, instrument_),
        };
    }

    return HttpResponse{
        400,
        http::encode_error_response(request.request_id, engine::result_code::malformed_request, "unsupported route"),
    };
}

}  // namespace order_match::server
