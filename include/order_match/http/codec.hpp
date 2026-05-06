#pragma once

#include "order_match/core/instrument.hpp"
#include "order_match/engine/event.hpp"

#include <string_view>

namespace order_match::http {

struct DecodeResult {
    engine::InboundEvent event{};
    engine::ResultCode result{engine::result_code::ok};
};

[[nodiscard]] DecodeResult decode_request(std::string_view method,
                                          std::string_view target,
                                          std::string_view body,
                                          const core::InstrumentConfig& instrument);

[[nodiscard]] std::string_view encode_result_code(engine::ResultCode result) noexcept;

}  // namespace order_match::http
