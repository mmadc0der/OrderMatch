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

}  // namespace
