#include "order_match/http/codec.hpp"

namespace order_match::http {

DecodeResult decode_request(std::string_view method,
                            std::string_view target,
                            std::string_view body) {
    static_cast<void>(method);
    static_cast<void>(target);
    static_cast<void>(body);

    DecodeResult result{};
    result.result = engine::result_code::rejected;
    return result;
}

std::string_view encode_result_code(const engine::ResultCode result) noexcept {
    switch (result) {
        case engine::result_code::ok:
            return "ok";
        case engine::result_code::rejected:
            return "rejected";
        case engine::result_code::malformed_request:
            return "malformed_request";
        case engine::result_code::invalid_price:
            return "invalid_price";
        case engine::result_code::invalid_quantity:
            return "invalid_quantity";
        case engine::result_code::invalid_order_type:
            return "invalid_order_type";
        case engine::result_code::invalid_time_in_force:
            return "invalid_time_in_force";
        case engine::result_code::not_found:
            return "not_found";
        case engine::result_code::already_terminal:
            return "already_terminal";
        case engine::result_code::insufficient_liquidity:
            return "insufficient_liquidity";
        case engine::result_code::overloaded:
            return "overloaded";
        default:
            return "unknown";
    }
}

}  // namespace order_match::http
