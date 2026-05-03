#pragma once

#include "order_match/core/types.hpp"

#include <vector>

namespace order_match::engine {

inline constexpr core::DepthLimit default_book_depth = 10U;
inline constexpr core::DepthLimit max_book_depth = 1000U;

struct TopOfBookView {
    core::PriceQuantity best_bid{};
    core::PriceQuantity best_ask{};
    core::SequenceNumber sequence{};
};

struct BookLevelView {
    core::PriceQuantity price_quantity{};
    std::uint32_t order_count{};
};

struct BookViewSnapshot {
    core::SequenceNumber sequence{};
    std::vector<BookLevelView> bids{};
    std::vector<BookLevelView> asks{};
};

class BookView {
public:
    [[nodiscard]] TopOfBookView top() const noexcept;
    [[nodiscard]] BookViewSnapshot bake(core::DepthLimit depth) const;

private:
    TopOfBookView top_{};
};

inline TopOfBookView BookView::top() const noexcept {
    return top_;
}

inline BookViewSnapshot BookView::bake(const core::DepthLimit depth) const {
    static_cast<void>(depth);

    BookViewSnapshot snapshot{};
    snapshot.sequence = top_.sequence;
    return snapshot;
}

}  // namespace order_match::engine
