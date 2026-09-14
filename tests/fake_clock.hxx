///
/// @file fake_clock.hxx
/// @brief Deterministic millisecond clock for tests, satisfies ba7lya::snowflake::ms_clock
///

#pragma once

#include <atomic>
#include <cstdint>

namespace snowflake_tests {

///
/// @brief Manually advanced fake clock. All copies share one atomic value; test fixtures reset it.
///
struct fake_clock {
    [[nodiscard]]
    static int64_t now_ms() noexcept {
        return value_.load(std::memory_order_relaxed);
    }

    static void set(int64_t ms) noexcept { value_.store(ms, std::memory_order_relaxed); }

    static void advance(int64_t ms) noexcept { value_.fetch_add(ms, std::memory_order_relaxed); }

    inline static std::atomic<int64_t> value_ { 0 };
};

} // namespace snowflake_tests
