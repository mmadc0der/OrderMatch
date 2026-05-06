#include "order_match/http/codec.hpp"

#include <gtest/gtest.h>

namespace {

using namespace order_match::core;
using namespace order_match::http;
namespace engine = order_match::engine;

[[nodiscard]] InstrumentConfig make_instrument() {
    InstrumentConfig instrument{};
    instrument.min_price = 1U;
    instrument.max_price = 1'000'000U;
    instrument.min_quantity = 1U;
    instrument.max_quantity = 1'000'000U;
    instrument.price_scale = 100U;
    instrument.quantity_scale = 100U;
    return instrument;
}

TEST(HttpCodecTest, DecodesLimitOrderRequest) {
    const auto decoded = decode_request(
        "POST",
        "/orders",
        R"({"side":"buy","type":"limit","time_in_force":"gtc","price":"101.25","quantity":"3.50"})",
        make_instrument());

    EXPECT_EQ(decoded.result, engine::result_code::ok);
    EXPECT_EQ(decoded.event.type, engine::event_type::submit_order);
    EXPECT_EQ(decoded.event.flags, 0U);
    EXPECT_EQ(decoded.event.price_quantity, pack_price_quantity(10125U, 350U));
}

TEST(HttpCodecTest, DecodesMarketOrderRequest) {
    const auto decoded = decode_request(
        "POST",
        "/orders",
        R"({"side":"sell","type":"market","time_in_force":"ioc","quantity":"2.00"})",
        make_instrument());

    EXPECT_EQ(decoded.result, engine::result_code::ok);
    EXPECT_EQ(decoded.event.type, engine::event_type::submit_order);
    EXPECT_EQ(decoded.event.flags,
              static_cast<engine::EventFlags>(engine::event_flags::side_sell |
                                               engine::event_flags::kind_market |
                                               engine::event_flags::tif_ioc));
    EXPECT_EQ(decoded.event.price_quantity, pack_price_quantity(0U, 200U));
}

TEST(HttpCodecTest, DecodesCancelRequest) {
    const auto decoded = decode_request("DELETE", "/orders/42", "{}", make_instrument());

    EXPECT_EQ(decoded.result, engine::result_code::ok);
    EXPECT_EQ(decoded.event.type, engine::event_type::cancel_order);
    EXPECT_EQ(decoded.event.order_id, 42U);
}

TEST(HttpCodecTest, RejectsMalformedOrderBody) {
    const auto decoded = decode_request("POST", "/orders", R"({"side":"buy","quantity":"1.00"})", make_instrument());

    EXPECT_EQ(decoded.result, engine::result_code::malformed_request);
}

TEST(HttpCodecTest, RejectsUnsupportedRoute) {
    const auto decoded = decode_request("GET", "/health", "", make_instrument());

    EXPECT_EQ(decoded.result, engine::result_code::malformed_request);
}

TEST(HttpCodecTest, EncodesResultCodes) {
    EXPECT_EQ(encode_result_code(engine::result_code::ok), "ok");
    EXPECT_EQ(encode_result_code(engine::result_code::malformed_request), "malformed_request");
    EXPECT_EQ(encode_result_code(engine::result_code::overloaded), "overloaded");
}

TEST(HttpCodecTest, EncodesExecutionReportWithFills) {
    engine::ExecutionReport report{};
    report.request_id = 12345U;
    report.order_id = 9001U;
    report.cumulative_quantity = 200U;
    report.leaves_quantity = 350U;
    report.sequence = 42U;
    report.result = engine::result_code::ok;
    report.status = engine::ExecutionStatus::resting;
    report.fills.push_back(engine::ExecutionFill{9001U, 8999U, pack_price_quantity(10120U, 200U), 42U});

    const auto json = encode_execution_report(report, make_instrument());

    EXPECT_EQ(json,
              R"({"request_id":12345,"result":"ok","order_id":9001,"status":"resting","cumulative_quantity":"2.00","leaves_quantity":"3.50","sequence":42,"fills":[{"maker_order_id":8999,"taker_order_id":9001,"price":"101.20","quantity":"2.00","sequence":42}]})");
}

TEST(HttpCodecTest, EncodesBookSnapshot) {
    engine::BookViewSnapshot snapshot{};
    snapshot.sequence = 46U;
    snapshot.bids.push_back(engine::BookLevelView{pack_price_quantity(10120U, 400U), 2U});
    snapshot.asks.push_back(engine::BookLevelView{pack_price_quantity(10130U, 150U), 1U});

    const auto json = encode_book_snapshot(snapshot, 10U, make_instrument());

    EXPECT_EQ(json,
              R"({"sequence":46,"depth":10,"bids":[{"price":"101.20","quantity":"4.00","order_count":2}],"asks":[{"price":"101.30","quantity":"1.50","order_count":1}]})");
}

TEST(HttpCodecTest, EncodesHealthResponse) {
    const auto json = encode_health_response(true, "running", 65536U, 65500U, 46U);

    EXPECT_EQ(json,
              R"({"ready":true,"engine":"running","inbound_queue":{"capacity":65536,"available":65500},"sequence":46})");
}

TEST(HttpCodecTest, EncodesErrorResponse) {
    const auto json = encode_error_response(99U, engine::result_code::invalid_price, "price is outside range");

    EXPECT_EQ(json,
              R"({"request_id":99,"result":"invalid_price","message":"price is outside range"})");
}

}  // namespace
