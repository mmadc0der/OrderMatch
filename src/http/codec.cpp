#include "order_match/http/codec.hpp"

#include <charconv>
#include <cstddef>
#include <cctype>
#include <limits>
#include <string>

namespace order_match::http {

namespace {

[[nodiscard]] std::string_view trim(std::string_view value) noexcept {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1U);
    }

    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1U);
    }

    return value;
}

[[nodiscard]] bool parse_unsigned(std::string_view text, core::OrderId& value) noexcept {
    text = trim(text);
    if (text.empty()) {
        return false;
    }

    core::OrderId parsed{};
    const auto* const begin = text.data();
    const auto* const end = begin + static_cast<std::ptrdiff_t>(text.size());
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }

    value = parsed;
    return true;
}

[[nodiscard]] bool extract_json_string(std::string_view body,
                                      std::string_view key,
                                      std::string_view& value) noexcept {
    const std::string needle = "\"" + std::string(key) + "\"";
    const auto key_pos = body.find(needle);
    if (key_pos == std::string_view::npos) {
        return false;
    }

    const auto colon_pos = body.find(':', key_pos + needle.size());
    if (colon_pos == std::string_view::npos) {
        return false;
    }

    auto value_pos = colon_pos + 1U;
    while (value_pos < body.size() && std::isspace(static_cast<unsigned char>(body[value_pos])) != 0) {
        ++value_pos;
    }

    if (value_pos >= body.size() || body[value_pos] != '"') {
        return false;
    }

    const auto end_pos = body.find('"', value_pos + 1U);
    if (end_pos == std::string_view::npos) {
        return false;
    }

    value = body.substr(value_pos + 1U, end_pos - value_pos - 1U);
    return true;
}

[[nodiscard]] constexpr bool is_power_of_ten(core::PriceTicks scale) noexcept {
    if (scale == 0U) {
        return false;
    }

    while (scale % 10U == 0U) {
        scale /= 10U;
    }

    return scale == 1U;
}

[[nodiscard]] constexpr std::size_t decimal_places(core::PriceTicks scale) noexcept {
    std::size_t places{};
    while (scale > 1U) {
        scale /= 10U;
        ++places;
    }

    return places;
}

template <typename UnsignedT>
[[nodiscard]] bool parse_scaled_decimal(std::string_view text, const UnsignedT scale, UnsignedT& value) noexcept {
    text = trim(text);
    if (text.empty() || !is_power_of_ten(scale)) {
        return false;
    }

    const auto dot_pos = text.find('.');
    const std::string_view whole_part = dot_pos == std::string_view::npos ? text : text.substr(0U, dot_pos);
    const std::string_view frac_part = dot_pos == std::string_view::npos ? std::string_view{} : text.substr(dot_pos + 1U);

    if (whole_part.empty()) {
        return false;
    }

    const auto places = decimal_places(scale);
    if (frac_part.size() > places) {
        return false;
    }

    UnsignedT whole{};
    const auto* const whole_begin = whole_part.data();
    const auto* const whole_end = whole_begin + static_cast<std::ptrdiff_t>(whole_part.size());
    const auto whole_result = std::from_chars(whole_begin, whole_end, whole);
    if (whole_result.ec != std::errc{} || whole_result.ptr != whole_end) {
        return false;
    }

    if (!frac_part.empty()) {
        for (const char ch : frac_part) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
                return false;
            }
        }
    }

    UnsignedT frac{};
    if (!frac_part.empty()) {
        const auto* const frac_begin = frac_part.data();
        const auto* const frac_end = frac_begin + static_cast<std::ptrdiff_t>(frac_part.size());
        const auto frac_result = std::from_chars(frac_begin, frac_end, frac);
        if (frac_result.ec != std::errc{} || frac_result.ptr != frac_end) {
            return false;
        }
    }

    UnsignedT fraction_multiplier = scale;
    for (std::size_t index = 0U; index < frac_part.size(); ++index) {
        fraction_multiplier /= 10U;
    }

    if (fraction_multiplier == 0U) {
        return false;
    }

    const auto max_value = std::numeric_limits<UnsignedT>::max();
    if (whole > max_value / scale) {
        return false;
    }

    const auto scaled_whole = whole * scale;
    if (frac > max_value / fraction_multiplier) {
        return false;
    }

    const auto scaled_fraction = frac * fraction_multiplier;
    if (scaled_whole > max_value - scaled_fraction) {
        return false;
    }

    value = scaled_whole + scaled_fraction;
    return true;
}

[[nodiscard]] bool parse_submit_request(const std::string_view body,
                                        const core::InstrumentConfig& instrument,
                                        engine::InboundEvent& event,
                                        engine::ResultCode& result) noexcept {
    std::string_view side{};
    std::string_view type{};
    std::string_view tif{};
    std::string_view price{};
    std::string_view quantity{};

    if (!extract_json_string(body, "side", side) ||
        !extract_json_string(body, "type", type) ||
        !extract_json_string(body, "time_in_force", tif) ||
        !extract_json_string(body, "quantity", quantity)) {
        result = engine::result_code::malformed_request;
        return false;
    }

    const bool is_market = type == "market";
    if (!is_market && !extract_json_string(body, "price", price)) {
        result = engine::result_code::malformed_request;
        return false;
    }

    core::PriceTicks parsed_price{};
    core::QuantityUnits parsed_quantity{};
    if (!parse_scaled_decimal(quantity, instrument.quantity_scale == 0U ? 1U : instrument.quantity_scale, parsed_quantity)) {
        result = engine::result_code::invalid_quantity;
        return false;
    }

    if (!is_market &&
        !parse_scaled_decimal(price, instrument.price_scale == 0U ? 1U : instrument.price_scale, parsed_price)) {
        result = engine::result_code::invalid_price;
        return false;
    }

    event.type = engine::event_type::submit_order;
    event.flags = 0U;
    if (side == "sell") {
        event.flags |= engine::event_flags::side_sell;
    } else if (side != "buy") {
        result = engine::result_code::invalid_order_type;
        return false;
    }

    if (is_market) {
        event.flags |= engine::event_flags::kind_market;
    } else if (type != "limit") {
        result = engine::result_code::invalid_order_type;
        return false;
    }

    if (tif == "ioc") {
        event.flags |= engine::event_flags::tif_ioc;
    } else if (tif == "fok") {
        event.flags |= engine::event_flags::tif_fok;
    } else if (tif != "gtc") {
        result = engine::result_code::invalid_time_in_force;
        return false;
    }

    event.price_quantity = core::pack_price_quantity(parsed_price, parsed_quantity);
    result = engine::result_code::ok;
    return true;
}

[[nodiscard]] bool parse_cancel_request(const std::string_view target,
                                        engine::InboundEvent& event,
                                        engine::ResultCode& result) noexcept {
    constexpr std::string_view prefix{"/orders/"};
    if (!target.starts_with(prefix)) {
        result = engine::result_code::malformed_request;
        return false;
    }

    core::OrderId order_id{};
    if (!parse_unsigned(target.substr(prefix.size()), order_id) || order_id == 0U) {
        result = engine::result_code::malformed_request;
        return false;
    }

    event.type = engine::event_type::cancel_order;
    event.order_id = order_id;
    result = engine::result_code::ok;
    return true;
}

}  // namespace

DecodeResult decode_request(std::string_view method,
                            std::string_view target,
                            std::string_view body,
                            const core::InstrumentConfig& instrument) {
    DecodeResult result{};
    result.event = {};

    if (method == "POST" && target == "/orders") {
        static_cast<void>(parse_submit_request(body, instrument, result.event, result.result));
        return result;
    }

    if (method == "DELETE") {
        static_cast<void>(parse_cancel_request(target, result.event, result.result));
        return result;
    }

    result.result = engine::result_code::malformed_request;
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
