#include "order_match/engine/engine_runner.hpp"

namespace order_match::engine {

ExecutionReport EngineRunner::process(const InboundEvent& event) {
    ExecutionReport report{};
    report.request_id = event.request_id;
    report.order_id = event.order_id;
    report.sequence = engine_.book().sequence();
    report.result = result_code::ok;
    return report;
}

BookViewSnapshot EngineRunner::bake_book_view(const core::DepthLimit depth) const {
    return book_view_.bake(depth);
}

TopOfBookView EngineRunner::top_of_book() const noexcept {
    return book_view_.top();
}

}  // namespace order_match::engine
