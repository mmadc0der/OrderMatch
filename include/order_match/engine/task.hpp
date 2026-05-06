#pragma once

#include <cstdint>
#include <functional>

namespace order_match::engine {

enum class TaskKind : std::uint8_t {
    core_aware = 0U,
    core_agnostic = 1U,
};

inline constexpr bool is_core_aware(const TaskKind kind) noexcept {
    return kind == TaskKind::core_aware;
}

struct EngineTask {
    TaskKind kind{TaskKind::core_aware};
    std::function<void()> work{};
};

}  // namespace order_match::engine
