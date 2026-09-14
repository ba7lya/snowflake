///
/// @file snowflake.hxx
/// @author BA7LYA (1042140025@qq.com)
/// @brief Snowflake ID generator (header-only, C++20)
/// @version 0.2
/// @date 2026-09-15
/// @copyright Copyright (c) 2026
/// @license MIT
///

#pragma once

#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace ba7lya::snowflake {

///
/// @brief ID generation algorithm selection.
///
enum class algo : std::uint8_t {
    drift,    ///< Drift algorithm: pushes the timestamp into the future when the per-ms
              /// sequence is exhausted; clock rollbacks are compensated with reserved
              /// sequence numbers 1-4
    original, ///< Classic snowflake: spins waiting for the next millisecond when the
              /// sequence is exhausted; clock rollbacks are NOT handled (see README warning)
};

///
/// @brief Generator configuration. All fields carry "pre-resolution" semantics;
/// validate_options() resolves sentinels (e.g. max_seq_num == 0) and validates ranges.
///
struct options {
    /// Generation algorithm, defaults to the drift algorithm
    algo algo { algo::drift };

    /// Base time in ms since Unix epoch. Must not be later than the current system
    /// time. 0 means "use default_base_time".
    uint64_t base_time { 1582136402000 };

    /// Machine id, must be set externally. Range: [0, 2^worker_id_bit_len - 1]
    uint32_t worker_id { 0 };

    /// Bit length of the machine id. Range: [1, 21], with seq_bit_len +
    /// worker_id_bit_len <= 22
    uint8_t worker_id_bit_len { 6 };

    /// Bit length of the sequence number. Range: [2, 21], with seq_bit_len +
    /// worker_id_bit_len <= 22
    uint8_t seq_bit_len { 6 };

    /// Maximum sequence number (inclusive). Range: [min_seq_num, 2^seq_bit_len - 1].
    /// 0 means "automatically use 2^seq_bit_len - 1".
    uint32_t max_seq_num { 0 };

    /// Minimum sequence number (inclusive), default 5. The first 5 sequence numbers
    /// of every millisecond (0-4) are reserved by protocol: 0 is kept for manual id
    /// injection, 1-4 are used as clock-rollback compensation indices.
    uint32_t min_seq_num { 5 };

    /// Maximum number of drifts within one period (inclusive).
    /// Range: [0, 10000], default 2000.
    uint32_t top_over_cost_cnt { 2000 };
};

inline constexpr uint64_t default_base_time { 1582136402000 }; ///< 2020-02-20 05:00:02 UTC
inline constexpr uint64_t min_base_time { 631123200000 };      ///< 1990-01-01 UTC
inline constexpr uint32_t max_top_over_cost_cnt { 10000 };
inline constexpr uint32_t reserved_seq_count { 5 }; ///< sequence numbers 0-4 are protocol-reserved
inline constexpr uint8_t max_bit_len_sum { 22 };    ///< keeps a 41-bit timestamp positive in int64

namespace detail {

inline std::string to_string(std::uint64_t value) { return std::to_string(value); }

} // namespace detail

///
/// @brief Validate and resolve an options object: sentinel values such as
/// base_time == 0 or max_seq_num == 0 are replaced by their effective values.
/// @param opts   user configuration (taken by value; the resolved copy is returned)
/// @param now_ms current Unix epoch milliseconds; injected for deterministic testing
/// @throw std::invalid_argument if any field is out of range
///
[[nodiscard]]
inline options validate_options(options opts, int64_t now_ms) {
    // 1. base_time
    if (opts.base_time == 0) { opts.base_time = default_base_time; }
    else if (
        std::cmp_less(opts.base_time, min_base_time) || std::cmp_greater(opts.base_time, now_ms)
    ) {
        throw std::invalid_argument(
            "snowflake: base_time error. (must be in [631123200000, now], or 0 for default)"
        );
    }

    // 2. seq_bit_len
    if (opts.seq_bit_len < 2 || opts.seq_bit_len > 21) {
        throw std::invalid_argument(
            "snowflake: seq_bit_len error. (range:[2, 21], got "
            + detail::to_string(opts.seq_bit_len) + ")"
        );
    }

    // 3. worker_id_bit_len
    if (opts.worker_id_bit_len < 1 || opts.worker_id_bit_len > 21) {
        throw std::invalid_argument(
            "snowflake: worker_id_bit_len error. (range:[1, 21], got "
            + detail::to_string(opts.worker_id_bit_len) + ")"
        );
    }

    // 4. bit length sum
    if (static_cast<uint32_t>(opts.seq_bit_len) + opts.worker_id_bit_len > max_bit_len_sum) {
        throw std::invalid_argument(
            "snowflake: seq_bit_len + worker_id_bit_len must be <= 22, got "
            + detail::to_string(opts.seq_bit_len) + " + "
            + detail::to_string(opts.worker_id_bit_len)
        );
    }

    // 5. max_seq_num: 0 resolves to 2^seq_bit_len - 1
    const uint32_t seq_limit = (1U << opts.seq_bit_len) - 1U;
    if (opts.max_seq_num > seq_limit) {
        throw std::invalid_argument(
            "snowflake: max_seq_num error. (range:[1, " + detail::to_string(seq_limit) + "], got "
            + detail::to_string(opts.max_seq_num) + ")"
        );
    }
    if (opts.max_seq_num == 0) { opts.max_seq_num = seq_limit; }

    // 6. min_seq_num: must lie in [reserved, resolved max]
    if (opts.min_seq_num < reserved_seq_count || opts.min_seq_num > opts.max_seq_num) {
        throw std::invalid_argument(
            "snowflake: min_seq_num error. (range:[" + detail::to_string(reserved_seq_count) + ", "
            + detail::to_string(opts.max_seq_num) + "], got " + detail::to_string(opts.min_seq_num)
            + ")"
        );
    }

    // 7. worker_id
    const uint32_t worker_limit = (1U << opts.worker_id_bit_len) - 1U;
    if (opts.worker_id > worker_limit) {
        throw std::invalid_argument(
            "snowflake: worker_id error. (range:[0, " + detail::to_string(worker_limit) + "], got "
            + detail::to_string(opts.worker_id) + ")"
        );
    }

    // 8. top_over_cost_cnt
    if (opts.top_over_cost_cnt > max_top_over_cost_cnt) {
        throw std::invalid_argument(
            "snowflake: top_over_cost_cnt error. (range:[0, 10000], got "
            + detail::to_string(opts.top_over_cost_cnt) + ")"
        );
    }

    return opts;
}

///
/// @brief Clock policy concept: supplies the current Unix epoch milliseconds.
/// Static member only => zero overhead on the hot path.
///
template<class C>
concept ms_clock = requires {
    { C::now_ms() } noexcept -> std::convertible_to<int64_t>;
};

///
/// @brief Default clock source: millisecond timestamp from std::chrono::system_clock.
///
struct system_clock_source {
    [[nodiscard]]
    static int64_t now_ms() noexcept {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch()
        )
            .count();
    }
};

///
/// @brief Bit-field layout of a generated id, for tests and diagnostics.
///
struct id_parts {
    int64_t time_tick; ///< milliseconds since base_time
    uint32_t worker_id;
    uint32_t seq;
};

///
/// @brief Thread-safe snowflake id generator.
///
/// Value semantics: one instance binds one worker_id; non-copyable and non-movable
/// (it holds a mutex). Construct multiple generators for multiple worker ids.
/// @tparam Clock millisecond clock policy, see ms_clock. Tests can inject a fake
/// clock to avoid real sleeping.
///
template<ms_clock Clock = system_clock_source>
class basic_generator {
public:
    /// @throw std::invalid_argument if options fail validation
    explicit basic_generator(options opts = {})
        : opts_ { validate_options(opts, Clock::now_ms()) }
        , ts_shift_ { static_cast<uint8_t>(opts_.worker_id_bit_len + opts_.seq_bit_len) }
        , curr_seq_num_ { opts_.min_seq_num } {}

    basic_generator(const basic_generator&) = delete;
    basic_generator& operator=(const basic_generator&) = delete;
    basic_generator(basic_generator&&) = delete;
    basic_generator& operator=(basic_generator&&) = delete;
    ~basic_generator() = default;

    ///
    /// @brief Generate the next id. Strictly increasing per worker (both algorithms).
    ///
    [[nodiscard]]
    int64_t next_id() {
        std::scoped_lock lock(mtx_);
        return opts_.algo == algo::original ? next_original_id() : next_drift_id();
    }

    /// @brief The resolved configuration.
    [[nodiscard]]
    const options& get_options() const noexcept {
        return opts_;
    }

    ///
    /// @brief Decode an id into (time_tick, worker_id, seq) bit fields.
    ///
    [[nodiscard]]
    id_parts decode(int64_t raw_id) const noexcept {
        const auto bits = static_cast<uint64_t>(raw_id);
        const uint64_t worker_mask = (1U << opts_.worker_id_bit_len) - 1U;
        const uint64_t seq_mask = (1U << opts_.seq_bit_len) - 1U;
        return {
            .time_tick = static_cast<int64_t>(bits >> ts_shift_),
            .worker_id = static_cast<uint32_t>((bits >> opts_.seq_bit_len) & worker_mask),
            .seq = static_cast<uint32_t>(bits & seq_mask),
        };
    }

private:
    [[nodiscard]]
    int64_t curr_time_tick() const noexcept {
        return Clock::now_ms() - static_cast<int64_t>(opts_.base_time);
    }

    /// Sleep in 1ms steps until the wall-clock tick is strictly greater than
    /// not_before.
    [[nodiscard]]
    int64_t wait_next_tick(int64_t not_before) const {
        int64_t tick = curr_time_tick();
        while (tick <= not_before) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            tick = curr_time_tick();
        }
        return tick;
    }

    /// Combine the bit fields and advance the sequence counter. A negative tick
    /// (rollback compensation running below base_time) wraps as unsigned, matching
    /// the original implementation's shift behavior while avoiding signed-shift UB.
    [[nodiscard]]
    int64_t calc_id() noexcept {
        const uint64_t result = (static_cast<uint64_t>(last_time_tick_) << ts_shift_)
                              | (static_cast<uint64_t>(opts_.worker_id) << opts_.seq_bit_len)
                              | curr_seq_num_;
        curr_seq_num_++;
        return static_cast<int64_t>(result);
    }

    /// Rollback compensation: seq is the fixed per-round index 1-4 (cycled), while
    /// the tick walks backwards below the high-water mark.
    [[nodiscard]]
    int64_t calc_turn_back_id() noexcept {
        const uint64_t result = (static_cast<uint64_t>(turn_back_time_tick_) << ts_shift_)
                              | (static_cast<uint64_t>(opts_.worker_id) << opts_.seq_bit_len)
                              | turn_back_idx_;
        turn_back_time_tick_--;
        return static_cast<int64_t>(result);
    }

    [[nodiscard]]
    int64_t next_drift_id() {
        return is_over_cost_ ? next_over_cost_id() : next_normal_id();
    }

    [[nodiscard]]
    int64_t next_normal_id() {
        const int64_t time_tick = curr_time_tick();

        if (time_tick < last_time_tick_) {
            // Enter / continue rollback compensation. turn_back_time_tick_ < 1 means
            // the previous rollback round is exhausted, so start a new round: the
            // index cycles 1-4, allowing unlimited rollbacks without collisions.
            if (turn_back_time_tick_ < 1) {
                turn_back_time_tick_ = last_time_tick_ - 1;
                turn_back_idx_++;
                if (turn_back_idx_ > 4) { turn_back_idx_ = 1; }
            }
            return calc_turn_back_id();
        }

        turn_back_time_tick_ = std::min(turn_back_time_tick_, int64_t { 0 });

        if (time_tick > last_time_tick_) {
            last_time_tick_ = time_tick;
            curr_seq_num_ = opts_.min_seq_num;
            return calc_id();
        }

        if (curr_seq_num_ > opts_.max_seq_num) {
            // Sequence exhausted for this millisecond: enter drift mode and push the
            // high-water mark one millisecond into the future.
            last_time_tick_++;
            curr_seq_num_ = opts_.min_seq_num;
            is_over_cost_ = true;
            over_cost_cnt_ = 1;
            return calc_id();
        }

        return calc_id();
    }

    [[nodiscard]]
    int64_t next_over_cost_id() {
        const int64_t time_tick = curr_time_tick();

        if (time_tick > last_time_tick_) {
            // Wall clock caught up with the drifted high-water mark: leave drift mode
            last_time_tick_ = time_tick;
            curr_seq_num_ = opts_.min_seq_num;
            is_over_cost_ = false;
            over_cost_cnt_ = 0;
            return calc_id();
        }

        if (over_cost_cnt_ > opts_.top_over_cost_cnt) {
            // Drift budget for this period exhausted: jump to the next available
            // millisecond (may sleep)
            last_time_tick_ = wait_next_tick(last_time_tick_);
            curr_seq_num_ = opts_.min_seq_num;
            is_over_cost_ = false;
            over_cost_cnt_ = 0;
            return calc_id();
        }

        if (curr_seq_num_ > opts_.max_seq_num) {
            last_time_tick_++;
            curr_seq_num_ = opts_.min_seq_num;
            over_cost_cnt_++;
            return calc_id();
        }

        return calc_id();
    }

    [[nodiscard]]
    int64_t next_original_id() {
        int64_t time_tick = curr_time_tick();

        if (time_tick == last_time_tick_) {
            curr_seq_num_++;
            if (curr_seq_num_ > opts_.max_seq_num) {
                // Sequence exhausted: block until the next millisecond. (The original
                // implementation used an AND-mask here, which is only correct when
                // max == 2^k - 1 and can emit reserved seq values 1-4.)
                time_tick = wait_next_tick(last_time_tick_);
                curr_seq_num_ = opts_.min_seq_num;
            }
        }
        else {
            // NOTE: clock rollbacks (time_tick < last_time_tick_) also land here --
            // the classic algorithm does not handle them and may repeat ids. See the
            // README warning; choose algo::drift if the clock may jump backwards.
            curr_seq_num_ = opts_.min_seq_num;
        }

        last_time_tick_ = time_tick;
        const uint64_t result = (static_cast<uint64_t>(time_tick) << ts_shift_)
                              | (static_cast<uint64_t>(opts_.worker_id) << opts_.seq_bit_len)
                              | curr_seq_num_;
        return static_cast<int64_t>(result);
    }

    std::mutex mtx_;
    options opts_;
    uint8_t ts_shift_ { 0 };
    uint32_t curr_seq_num_ { 0 };
    int64_t last_time_tick_ { 0 };
    int64_t turn_back_time_tick_ { 0 };
    uint8_t turn_back_idx_ { 0 };
    bool is_over_cost_ { false };
    uint32_t over_cost_cnt_ { 0 };
};

using generator = basic_generator<>;

} // namespace ba7lya::snowflake
