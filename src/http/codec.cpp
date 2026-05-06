#include "order_match/http/codec.hpp"

#include <algorithm>
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

[[nodiscard]] std::string_view encode_execution_status(const engine::ExecutionStatus status) noexcept {
    switch (status) {
    case engine::ExecutionStatus::resting:
        return "resting";
    case engine::ExecutionStatus::filled:
        return "filled";
    case engine::ExecutionStatus::cancelled:
        return "cancelled";
    case engine::ExecutionStatus::none:
    default:
        return "none";
    }
}

template <typename UnsignedT>
void append_unsigned(std::string& out, const UnsignedT value) {
    out += std::to_string(value);
}

template <typename UnsignedT>
void append_decimal(std::string& out, const UnsignedT value, const UnsignedT scale) {
    if (scale == 0U) {
        out += "0";
        return;
    }

    const auto whole = value / scale;
    const auto fraction = value % scale;
    out += std::to_string(whole);
    if (scale == 1U) {
        return;
    }

    const auto places = decimal_places(scale);
    const auto fraction_text = std::to_string(fraction);
    out.push_back('.');
    if (fraction_text.size() < places) {
        out.append(places - fraction_text.size(), '0');
    }
    out += fraction_text;
}

void append_json_string(std::string& out, std::string_view value) {
    out.push_back('"');
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(ch);
            break;
        }
    }
    out.push_back('"');
}

void append_field_prefix(std::string& out, bool& first) {
    if (!first) {
        out.push_back(',');
    }
    first = false;
}

template <typename UnsignedT>
void append_uint_field(std::string& out, std::string_view key, UnsignedT value, bool& first) {
    append_field_prefix(out, first);
    append_json_string(out, key);
    out.push_back(':');
    append_unsigned(out, value);
}

void append_string_field(std::string& out, std::string_view key, std::string_view value, bool& first) {
    append_field_prefix(out, first);
    append_json_string(out, key);
    out.push_back(':');
    append_json_string(out, value);
}

void append_bool_field(std::string& out, std::string_view key, bool value, bool& first) {
    append_field_prefix(out, first);
    append_json_string(out, key);
    out.push_back(':');
    out += value ? "true" : "false";
}

template <typename UnsignedT>
void append_decimal_string_field(std::string& out,
                                 std::string_view key,
                                 UnsignedT value,
                                 UnsignedT scale,
                                 bool& first) {
    append_field_prefix(out, first);
    append_json_string(out, key);
    out.push_back(':');
    out.push_back('"');
    append_decimal(out, value, scale);
    out.push_back('"');
}

void append_fill(std::string& out, const engine::ExecutionFill& fill, const core::InstrumentConfig& instrument) {
    out.push_back('{');
    bool first = true;
    append_uint_field(out, "maker_order_id", fill.maker_order_id, first);
    append_uint_field(out, "taker_order_id", fill.taker_order_id, first);
    append_decimal_string_field(out,
                                "price",
                                core::unpack_price(fill.price_quantity),
                                instrument.price_scale == 0U ? 1U : instrument.price_scale,
                                first);
    append_decimal_string_field(out,
                                "quantity",
                                core::unpack_quantity(fill.price_quantity),
                                instrument.quantity_scale == 0U ? 1U : instrument.quantity_scale,
                                first);
    append_uint_field(out, "sequence", fill.sequence, first);
    out.push_back('}');
}

void append_level(std::string& out, const engine::BookLevelView& level, const core::InstrumentConfig& instrument) {
    out.push_back('{');
    bool first = true;
    append_decimal_string_field(out,
                                "price",
                                core::unpack_price(level.price_quantity),
                                instrument.price_scale == 0U ? 1U : instrument.price_scale,
                                first);
    append_decimal_string_field(out,
                                "quantity",
                                core::unpack_quantity(level.price_quantity),
                                instrument.quantity_scale == 0U ? 1U : instrument.quantity_scale,
                                first);
    append_uint_field(out, "order_count", level.order_count, first);
    out.push_back('}');
}

template <typename Levels>
void append_levels(std::string& out, std::string_view key, const Levels& levels, const core::InstrumentConfig& instrument) {
    append_json_string(out, key);
    out.push_back(':');
    out.push_back('[');
    for (std::size_t index = 0U; index < levels.size(); ++index) {
        if (index != 0U) {
            out.push_back(',');
        }
        append_level(out, levels[index], instrument);
    }
    out.push_back(']');
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

std::string encode_execution_report(const engine::ExecutionReport& report,
                                    const core::InstrumentConfig& instrument) {
    std::string json{};
    json.push_back('{');
    bool first = true;
    append_uint_field(json, "request_id", report.request_id, first);
    append_string_field(json, "result", encode_result_code(report.result), first);
    append_uint_field(json, "order_id", report.order_id, first);
    append_string_field(json, "status", encode_execution_status(report.status), first);
    append_decimal_string_field(json,
                                "cumulative_quantity",
                                report.cumulative_quantity,
                                instrument.quantity_scale == 0U ? 1U : instrument.quantity_scale,
                                first);
    append_decimal_string_field(json,
                                "leaves_quantity",
                                report.leaves_quantity,
                                instrument.quantity_scale == 0U ? 1U : instrument.quantity_scale,
                                first);
    append_uint_field(json, "sequence", report.sequence, first);
    append_field_prefix(json, first);
    append_json_string(json, "fills");
    json.push_back(':');
    json.push_back('[');
    for (std::size_t index = 0U; index < report.fills.size(); ++index) {
        if (index != 0U) {
            json.push_back(',');
        }
        append_fill(json, report.fills[index], instrument);
    }
    json.push_back(']');
    json.push_back('}');
    return json;
}

std::string encode_book_snapshot(const engine::BookViewSnapshot& snapshot,
                                 const core::DepthLimit depth,
                                 const core::InstrumentConfig& instrument) {
    std::string json{};
    json.push_back('{');
    bool first = true;
    append_uint_field(json, "sequence", snapshot.sequence, first);
    append_uint_field(json, "depth", depth, first);
    append_field_prefix(json, first);
    append_levels(json, "bids", snapshot.bids, instrument);
    json.push_back(',');
    append_levels(json, "asks", snapshot.asks, instrument);
    json.push_back('}');
    return json;
}

std::string encode_health_response(const bool ready,
                                   std::string_view engine_state,
                                   const std::size_t inbound_capacity,
                                   const std::size_t inbound_available,
                                   const core::SequenceNumber sequence) {
    std::string json{};
    json.push_back('{');
    bool first = true;
    append_bool_field(json, "ready", ready, first);
    append_string_field(json, "engine", engine_state, first);
    append_field_prefix(json, first);
    append_json_string(json, "inbound_queue");
    json.push_back(':');
    json.push_back('{');
    bool queue_first = true;
    append_uint_field(json, "capacity", inbound_capacity, queue_first);
    append_uint_field(json, "available", inbound_available, queue_first);
    json.push_back('}');
    append_uint_field(json, "sequence", sequence, first);
    json.push_back('}');
    return json;
}

std::string encode_error_response(const core::RequestId request_id,
                                  const engine::ResultCode result,
                                  std::string_view message) {
    std::string json{};
    json.push_back('{');
    bool first = true;
    append_uint_field(json, "request_id", request_id, first);
    append_string_field(json, "result", encode_result_code(result), first);
    append_string_field(json, "message", message, first);
    json.push_back('}');
    return json;
}

}  // namespace order_match::http
