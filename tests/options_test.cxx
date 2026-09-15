///
/// @file options_test.cxx
/// @brief validate_options and constructor validation; includes regression tests
/// for the old implementation's max_seq_num / min_seq_num bugs (B1/B2)
///

#include <ba7lya/snowflake/snowflake.hxx>
#include <gtest/gtest.h>
#include <stdexcept>

#include "fake_clock.hxx"

using namespace ba7lya::snowflake;
using snowflake_tests::fake_clock;

namespace {

constexpr int64_t reference_now_ms = 1778000000000; // a fixed ms timestamp in 2026

} // namespace

TEST(options_test, defaults_resolve) {
    const options resolved = validate_options(options(), reference_now_ms);
    EXPECT_EQ(resolved.max_seq_num, 63U);
    EXPECT_EQ(resolved.min_seq_num, 5U);
    EXPECT_EQ(resolved.base_time, default_base_time);
    EXPECT_EQ(resolved.algo, algorithm::drift);
    EXPECT_EQ(resolved.worker_id_bit_len, 6);
    EXPECT_EQ(resolved.seq_bit_len, 6);
}

TEST(options_test, base_time_zero_uses_default) {
    options o;
    o.base_time = 0;
    EXPECT_EQ(validate_options(o, reference_now_ms).base_time, default_base_time);
}

TEST(options_test, base_time_too_old_throws) {
    options o;
    o.base_time = min_base_time - 1U;
    EXPECT_THROW((void)validate_options(o, reference_now_ms), std::invalid_argument);
}

TEST(options_test, base_time_in_future_throws) {
    options o;
    o.base_time = static_cast<uint64_t>(reference_now_ms) + 1;
    EXPECT_THROW((void)validate_options(o, reference_now_ms), std::invalid_argument);
}

TEST(options_test, seq_bit_len_out_of_range_throws) {
    options o;
    o.seq_bit_len = 1;
    EXPECT_THROW((void)validate_options(o, reference_now_ms), std::invalid_argument);
    o.seq_bit_len = 22;
    EXPECT_THROW((void)validate_options(o, reference_now_ms), std::invalid_argument);
}

TEST(options_test, worker_id_bit_len_out_of_range_throws) {
    options o;
    o.worker_id_bit_len = 0;
    EXPECT_THROW((void)validate_options(o, reference_now_ms), std::invalid_argument);
    o.worker_id_bit_len = 22;
    EXPECT_THROW((void)validate_options(o, reference_now_ms), std::invalid_argument);
}

TEST(options_test, bit_len_sum_too_large_throws) {
    options o;
    o.worker_id_bit_len = 11;
    o.seq_bit_len = 12; // sum is 23 > 22
    EXPECT_THROW((void)validate_options(o, reference_now_ms), std::invalid_argument);
}

TEST(options_test, max_seq_num_out_of_range_throws) {
    options o;
    o.max_seq_num = 64; // seq_bit_len=6 caps at 2^6-1 = 63
    EXPECT_THROW((void)validate_options(o, reference_now_ms), std::invalid_argument);
}

TEST(options_test, min_seq_num_too_small_throws) {
    options o;
    o.min_seq_num = 4; // sequence numbers 0-4 are protocol-reserved
    EXPECT_THROW((void)validate_options(o, reference_now_ms), std::invalid_argument);
}

TEST(options_test, min_seq_num_exceeds_resolved_max_throws) {
    // B2 regression: the old code compared against an unresolved local variable;
    // the new code resolves max_seq_num first and validates min against it
    options o;
    o.seq_bit_len = 3; // resolved max_seq_num = 2^3-1 = 7
    o.max_seq_num = 0; // 0 -> auto-resolve
    o.min_seq_num = 8; // > resolved 7, must throw
    EXPECT_THROW((void)validate_options(o, reference_now_ms), std::invalid_argument);
}

TEST(options_test, worker_id_out_of_range_throws) {
    options o;
    o.worker_id = 64; // worker_id_bit_len=6, max = 2^6-1 = 63
    EXPECT_THROW((void)validate_options(o, reference_now_ms), std::invalid_argument);
}

TEST(options_test, top_over_cost_cnt_too_large_throws) {
    options o;
    o.top_over_cost_cnt = max_top_over_cost_cnt + 1;
    EXPECT_THROW((void)validate_options(o, reference_now_ms), std::invalid_argument);
}

TEST(options_test, boundary_values_accepted) {
    options o;
    o.worker_id_bit_len = 16;
    o.seq_bit_len = 6;     // sum = 22 (the limit)
    o.worker_id = 0xFFFFU; // 2^16 - 1
    EXPECT_NO_THROW((void)validate_options(o, reference_now_ms));
}

TEST(options_test, tiny_seq_bit_len_leaves_no_legal_min_seq_num) {
    // seq_bit_len=2 => max=3 < 5, so no legal min_seq_num exists and validation must
    // throw -- the reserved-range protocol makes this configuration impossible by design
    options o;
    o.seq_bit_len = 2;
    EXPECT_THROW((void)validate_options(o, reference_now_ms), std::invalid_argument);
}

TEST(options_test, constructor_validates_and_throws) {
    fake_clock::set(reference_now_ms);
    options o;
    o.worker_id = 99999;
    EXPECT_THROW(const basic_generator<fake_clock> gen(o), std::invalid_argument);
}

// ---- B1 regression: the old opts_.max_seq_num stayed 0, so the seq of a second id
// within the same tick could never be the first one's + 1. ----

TEST(options_regression_test, consecutive_seq_in_same_tick_drift) {
    fake_clock::set(reference_now_ms);
    options o;
    o.algo = algorithm::drift;
    basic_generator<fake_clock> gen(o);
    const auto first = gen.decode(gen.next_id());
    const auto second = gen.decode(gen.next_id());
    EXPECT_EQ(first.time_tick, second.time_tick);
    EXPECT_EQ(second.seq, first.seq + 1);
}

TEST(options_regression_test, consecutive_seq_in_same_tick_original) {
    fake_clock::set(reference_now_ms);
    options o;
    o.algo = algorithm::original;
    basic_generator<fake_clock> gen(o);
    const auto first = gen.decode(gen.next_id());
    const auto second = gen.decode(gen.next_id());
    EXPECT_EQ(first.time_tick, second.time_tick);
    EXPECT_EQ(second.seq, first.seq + 1);
}
