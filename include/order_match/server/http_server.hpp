#pragma once

#include "order_match/server/http_pipeline.hpp"

namespace order_match::server {

class HttpServer {
public:
    HttpServer(engine::EngineRuntime& runtime, const core::InstrumentConfig& instrument) noexcept
        : pipeline_(runtime, instrument) {}

    [[nodiscard]] HttpResponse handle(const HttpRequest& request) {
        return pipeline_.handle(request);
    }

private:
    HttpPipeline pipeline_;
};

}  // namespace order_match::server
