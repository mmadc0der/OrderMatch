#include "order_match/server/http_pipeline.hpp"

#include <gtest/gtest.h>

#include <utility>

namespace {

using namespace order_match::core;
using namespace order_match::server;
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

[[nodiscard]] HttpRequest make_request(std::string method,
                                       std::string target,
                                       std::string body,
                                       RequestId request_id) {
    return HttpRequest{request_id, std::move(method), std::move(target), std::move(body)};
}

TEST(HttpPipelineTest, HandlesOrderAndCancelRequests) {
    engine::EngineRuntime runtime{8U, 8U, 8U, 1U};
    HttpPipeline pipeline{runtime, make_instrument()};

    const auto submit = pipeline.handle(make_request(
        "POST",
        "/orders",
        R"({"side":"buy","type":"limit","time_in_force":"gtc","price":"101.25","quantity":"3.50"})",
        123U));
    const auto cancel = pipeline.handle(make_request("DELETE", "/orders/1", "{}", 124U));

    EXPECT_EQ(submit.status_code, 200);
    EXPECT_EQ(submit.body,
              R"({"request_id":123,"result":"ok","order_id":1,"status":"resting","cumulative_quantity":"0.00","leaves_quantity":"3.50","sequence":1,"fills":[]})");
    EXPECT_EQ(cancel.status_code, 200);
    EXPECT_EQ(cancel.body,
              R"({"request_id":124,"result":"ok","order_id":1,"status":"cancelled","cumulative_quantity":"0.00","leaves_quantity":"0.00","sequence":2,"fills":[]})");
}

TEST(HttpPipelineTest, ServesBookAndHealthRequests) {
    engine::EngineRuntime runtime{8U, 8U, 8U, 1U};
    HttpPipeline pipeline{runtime, make_instrument()};

    const auto submit = pipeline.handle(make_request(
        "POST",
        "/orders",
        R"({"side":"sell","type":"limit","time_in_force":"gtc","price":"100.50","quantity":"2.00"})",
        200U));
    const auto book = pipeline.handle(make_request("GET", "/book?depth=1", "", 201U));
    const auto health = pipeline.handle(make_request("GET", "/health", "", 202U));

    EXPECT_EQ(submit.status_code, 200);
    EXPECT_EQ(book.status_code, 200);
    EXPECT_EQ(book.body,
              R"({"sequence":1,"depth":1,"bids":[],"asks":[{"price":"100.50","quantity":"2.00","order_count":1}]})");
    EXPECT_EQ(health.status_code, 200);
    EXPECT_EQ(health.body,
              R"({"ready":true,"engine":"running","inbound_queue":{"capacity":8,"available":8},"sequence":1})");
}

TEST(HttpPipelineTest, RejectsInvalidRoutesAndDepth) {
    engine::EngineRuntime runtime{8U, 8U, 8U, 1U};
    HttpPipeline pipeline{runtime, make_instrument()};

    const auto invalid_depth = pipeline.handle(make_request("GET", "/book?depth=0", "", 300U));
    const auto invalid_order = pipeline.handle(make_request("POST", "/orders", R"({"side":"buy","quantity":"1.00"})", 301U));
    const auto unsupported = pipeline.handle(make_request("PUT", "/orders", "{}", 302U));

    EXPECT_EQ(invalid_depth.status_code, 400);
    EXPECT_EQ(invalid_depth.body,
              R"({"request_id":300,"result":"malformed_request","message":"invalid book depth"})");
    EXPECT_EQ(invalid_order.status_code, 400);
    EXPECT_EQ(invalid_order.body,
              R"({"request_id":301,"result":"malformed_request","message":"malformed request"})");
    EXPECT_EQ(unsupported.status_code, 400);
    EXPECT_EQ(unsupported.body,
              R"({"request_id":302,"result":"malformed_request","message":"unsupported route"})");
}

}  // namespace
