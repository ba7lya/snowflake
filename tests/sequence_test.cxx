///
/// @file sequence_test.cxx
/// @brief Sequence-exhaustion behavior: drift pushes forward (no sleeping), original
/// blocks until the next millisecond
///

#include <atomic>
#include <ba7lya/snowflake/snowflake.hxx>
#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <thread>
#include <unordered_set>

#include "fake_clock.hxx"

using namespace ba7lya::snowflake;
using snowflake_tests::fake_clock;

namespace { constexpr int64_t reference_now_ms = 1778000000000; } // namespace

class sequence_test : public ::testing::Test {
protected:
    void SetUp() override { fake_clock::set(reference_now_ms); }
};

TEST_F(sequence_test, drift_fills_same_tick_then_drifts_forward_without_sleeping) {
    options o;
    o.seq_bit_len = 3; // max = 7
    o.min_seq_num = 5; // available sequences: 5,6,7
    basic_generator<fake_clock> gen(o);

    const int64_t tick0 = reference_now_ms - static_cast<int64_t>(default_base_time);
    std::unordered_set<int64_t> ids;
    for (uint32_t seq = 5; seq <= 7; ++seq) {
        const auto parts = gen.decode(gen.next_id());
        EXPECT_EQ(parts.time_tick, tick0);
        EXPECT_EQ(parts.seq, seq);
        ids.insert(parts.time_tick);
    }

    // this millisecond is exhausted: the next id drifts to tick0+1 with seq back at
    // min -- without blocking
    const auto drifted = gen.decode(gen.next_id());
    EXPECT_EQ(drifted.time_tick, tick0 + 1);
    EXPECT_EQ(drifted.seq, 5U);
}

TEST_F(sequence_test, drift_high_water_runs_ahead_of_wall_clock) {
    options o;
    o.seq_bit_len = 3; // 3 sequences per ms: 5,6,7
    basic_generator<fake_clock> gen(o);

    // frozen fake clock: after 4 ids the high-water mark legitimately runs ahead of the
    // wall clock and generation still completes instantly (no sleeping happens,
    // because the drift count stays far below the top_over_cost_cnt limit)
    id_parts last { .time_tick = 0, .worker_id = 0, .seq = 0 };
    for (int i = 0; i < 6; ++i) { last = gen.decode(gen.next_id()); }
    EXPECT_GT(last.time_tick, reference_now_ms - static_cast<int64_t>(default_base_time));
}

TEST_F(sequence_test, original_blocks_until_clock_advances) {
    options o;
    o.algo = algorithm::original;
    o.seq_bit_len = 3;
    o.min_seq_num = 5; // 3 sequences per ms: 5,6,7
    basic_generator<fake_clock> gen(o);

    for (uint32_t seq = 5; seq <= 7; ++seq) { EXPECT_EQ(gen.decode(gen.next_id()).seq, seq); }

    // the 4th call will block until the fake clock advances by 1ms
    std::atomic<bool> done { false };
    std::jthread waiter(
        [&]
        {
            const auto parts = gen.decode(gen.next_id());
            EXPECT_EQ(parts.seq, 5U);
            done.store(true);
        }
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(done.load()); // confirm it really blocked on the wait

    fake_clock::advance(1);
    waiter.join();
    EXPECT_TRUE(done.load());
}

TEST_F(sequence_test, advancing_clock_never_repeats) {
    // fake clock advances 1ms per id: the control group where the sequence never exhausts
    options o;
    o.seq_bit_len = 3;
    std::unordered_set<int64_t> ids;
    basic_generator<fake_clock> gen(o);
    for (int i = 0; i < 100; ++i) {
        ids.insert(gen.next_id());
        fake_clock::advance(1);
    }
    EXPECT_EQ(ids.size(), 100U);
}
