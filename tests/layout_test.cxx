///
/// @file layout_test.cxx
/// @brief ID bit-field layout and decode() correctness
///

#include <ba7lya/snowflake/snowflake.hxx>
#include <gtest/gtest.h>

#include "fake_clock.hxx"

using namespace ba7lya::snowflake;
using snowflake_tests::fake_clock;

namespace {
constexpr int64_t reference_now_ms = 1778000000000;

// the largest value a 41-bit timestamp field can hold
constexpr uint64_t max_tick = (uint64_t { 1 } << 41U) - 1U;
} // namespace

class layout_test : public ::testing::Test {
protected:
    void SetUp() override { fake_clock::set(reference_now_ms); }
};

TEST_F(layout_test, default_layout_matches_fake_clock) {
    options o;
    o.worker_id = 42;
    basic_generator<fake_clock> gen(o);
    const int64_t id = gen.next_id();
    const auto parts = gen.decode(id);

    EXPECT_EQ(parts.time_tick, reference_now_ms - static_cast<int64_t>(default_base_time));
    EXPECT_EQ(parts.worker_id, 42U);
    EXPECT_EQ(parts.seq, 5U); // first normal sequence number of every millisecond = min_seq_num
}

TEST_F(layout_test, non_default_bit_widths) {
    options o;
    o.worker_id_bit_len = 4; // ts_shift = 4+3 = 7
    o.seq_bit_len = 3;       // max_seq = 7
    o.worker_id = 9;
    o.max_seq_num = 6;
    o.min_seq_num = 5;
    basic_generator<fake_clock> gen(o);
    const auto parts = gen.decode(gen.next_id());

    EXPECT_EQ(parts.time_tick, reference_now_ms - static_cast<int64_t>(default_base_time));
    EXPECT_EQ(parts.worker_id, 9U);
    EXPECT_EQ(parts.seq, 5U);
}

TEST_F(layout_test, sign_bit_never_set_for_realistic_ticks) {
    options o;
    o.worker_id = 63;
    basic_generator<fake_clock> gen(o);
    // 41-bit timestamp + 22 bits: no int64 overflow for ~69 years
    fake_clock::set(static_cast<int64_t>(default_base_time) + max_tick - 1);
    EXPECT_GT(gen.next_id(), 0);
}

TEST_F(layout_test, manual_bit_composition) {
    // independently verify the bit composition: id = tick<<ts | worker<<seq_bits | seq
    options o;
    o.worker_id_bit_len = 6;
    o.seq_bit_len = 6;
    o.worker_id = 3;
    basic_generator<fake_clock> gen(o);
    const int64_t id = gen.next_id();
    const auto tick
        = static_cast<uint64_t>(reference_now_ms - static_cast<int64_t>(default_base_time));
    const uint64_t expected = (tick << 12U) | (3U << 6U) | 5U;
    EXPECT_EQ(static_cast<uint64_t>(id), expected);
}
