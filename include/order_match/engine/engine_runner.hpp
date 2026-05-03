#pragma once

#include "order_match/core/matching_engine.hpp"
#include "order_match/engine/event.hpp"
#include "order_match/engine/execution_report.hpp"
#include "order_match/engine/read_view.hpp"

namespace order_match::engine {

class EngineRunner {
public:
    EngineRunner() = default;

    [[nodiscard]] ExecutionReport process(const InboundEvent& event);
    [[nodiscard]] BookViewSnapshot bake_book_view(core::DepthLimit depth) const;
    [[nodiscard]] TopOfBookView top_of_book() const noexcept;

private:
    core::MatchingEngine engine_{};
    BookView book_view_{};
};

}  // namespace order_match::engine
