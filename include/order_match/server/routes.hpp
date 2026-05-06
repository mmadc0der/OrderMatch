#pragma once

#include "order_match/core/types.hpp"
#include "order_match/engine/read_view.hpp"

#include <charconv>
#include <optional>
#include <string_view>

namespace order_match::server {

class Routes {
public:
    [[nodiscard]] static bool is_health(std::string_view method, std::string_view target) noexcept;
    [[nodiscard]] static bool is_book(std::string_view method, std::string_view target) noexcept;
    [[nodiscard]] static bool is_order_request(std::string_view method, std::string_view target) noexcept;
    [[nodiscard]] static std::optional<core::DepthLimit> parse_book_depth(std::string_view target) noexcept;
};

inline bool Routes::is_health(const std::string_view method, const std::string_view target) noexcept {
    return method == "GET" && target == "/health";
}

inline bool Routes::is_book(const std::string_view method, const std::string_view target) noexcept {
    return method == "GET" && target.starts_with("/book");
}

inline bool Routes::is_order_request(const std::string_view method, const std::string_view target) noexcept {
    return (method == "POST" && target == "/orders") || (method == "DELETE" && target.starts_with("/orders/"));
}

inline std::optional<core::DepthLimit> Routes::parse_book_depth(const std::string_view target) noexcept {
    constexpr std::string_view prefix{"/book"};
    if (target == prefix) {
        return engine::default_book_depth;
    }

    if (!target.starts_with(prefix) || target.size() <= prefix.size() || target[prefix.size()] != '?') {
        return std::nullopt;
    }

    constexpr std::string_view depth_key{"depth="};
    const auto query = target.substr(prefix.size() + 1U);
    if (!query.starts_with(depth_key)) {
        return std::nullopt;
    }

    const auto depth_text = query.substr(depth_key.size());
    if (depth_text.empty()) {
        return std::nullopt;
    }

    core::DepthLimit depth{};
    const auto* const begin = depth_text.data();
    const auto* const end = begin + static_cast<std::ptrdiff_t>(depth_text.size());
    const auto result = std::from_chars(begin, end, depth);
    if (result.ec != std::errc{} || result.ptr != end || depth == 0U || depth > engine::max_book_depth) {
        return std::nullopt;
    }

    return depth;
}

}  // namespace order_match::server
