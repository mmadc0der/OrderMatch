#pragma once

#include "order_match/core/order.hpp"
#include "order_match/core/types.hpp"

#include <algorithm>
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
    void publish(const core::BookSnapshot& snapshot);
    [[nodiscard]] TopOfBookView top() const noexcept;
    [[nodiscard]] BookViewSnapshot bake(core::DepthLimit depth) const;

private:
    TopOfBookView top_{};
    BookViewSnapshot cached_{};
};

inline void BookView::publish(const core::BookSnapshot& snapshot) {
    cached_.sequence = snapshot.sequence;
    cached_.bids.clear();
    cached_.asks.clear();
    cached_.bids.reserve(std::min(snapshot.bids.size(), static_cast<std::size_t>(max_book_depth)));
    cached_.asks.reserve(std::min(snapshot.asks.size(), static_cast<std::size_t>(max_book_depth)));

    const auto append_levels = [](const auto& source, std::vector<BookLevelView>& output) {
        const auto limit = std::min(source.size(), static_cast<std::size_t>(max_book_depth));
        for (std::size_t index = 0U; index < limit; ++index) {
            const auto& level = source[index];
            output.push_back(BookLevelView{
                core::pack_price_quantity(level.price, level.quantity),
                level.order_count,
            });
        }
    };

    append_levels(snapshot.bids, cached_.bids);
    append_levels(snapshot.asks, cached_.asks);

    top_.sequence = snapshot.sequence;
    top_.best_bid = cached_.bids.empty() ? core::PriceQuantity{} : cached_.bids.front().price_quantity;
    top_.best_ask = cached_.asks.empty() ? core::PriceQuantity{} : cached_.asks.front().price_quantity;
}

inline TopOfBookView BookView::top() const noexcept {
    return top_;
}

inline BookViewSnapshot BookView::bake(const core::DepthLimit depth) const {
    BookViewSnapshot snapshot{};
    snapshot.sequence = cached_.sequence;
    if (depth == 0U) {
        return snapshot;
    }

    const auto limit = std::min<std::size_t>(static_cast<std::size_t>(depth), static_cast<std::size_t>(max_book_depth));
    const auto clip_levels = [limit](const std::vector<BookLevelView>& source, std::vector<BookLevelView>& output) {
        output.reserve(std::min(source.size(), limit));
        const auto count = std::min(source.size(), limit);
        output.insert(output.end(), source.begin(), source.begin() + static_cast<std::ptrdiff_t>(count));
    };

    clip_levels(cached_.bids, snapshot.bids);
    clip_levels(cached_.asks, snapshot.asks);
    return snapshot;
}

}  // namespace order_match::engine
