#pragma once

#include "order_match/core/instrument.hpp"
#include "order_match/engine/event.hpp"
#include "order_match/engine/execution_report.hpp"
#include "order_match/engine/read_view.hpp"

#include <string>
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
[[nodiscard]] std::string encode_execution_report(const engine::ExecutionReport& report,
                                                  const core::InstrumentConfig& instrument);
[[nodiscard]] std::string encode_book_snapshot(const engine::BookViewSnapshot& snapshot,
                                               core::DepthLimit depth,
                                               const core::InstrumentConfig& instrument);
[[nodiscard]] std::string encode_health_response(bool ready,
                                                 std::string_view engine_state,
                                                 std::size_t inbound_capacity,
                                                 std::size_t inbound_available,
                                                 core::SequenceNumber sequence);
[[nodiscard]] std::string encode_error_response(core::RequestId request_id,
                                                engine::ResultCode result,
                                                std::string_view message);

}  // namespace order_match::http
