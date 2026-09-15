///
/// @file rollback_test.cxx
/// @brief Clock rollback: drift compensates with reserved seq 1-4 and never repeats;
/// original is silently unsafe (documented behavior, pinned by tests)
///

#include <cstdint>
#include <gtest/gtest.h>
#include <snowflake.hxx>
#include <unordered_set>

#include "fake_clock.hxx"

using namespace ba7lya::snowflake;
using snowflake_tests::fake_clock;

namespace { constexpr int64_t reference_now_ms = 1778000000000; } // namespace

class rollback_test : public ::testing::Test {
protected:
    void SetUp() override { fake_clock::set(reference_now_ms); }
};

TEST_F(rollback_test, drift_handles_rollback_without_duplicates) {
    options o;
    o.worker_id = 5;
    basic_generator<fake_clock> gen(o);

    std::unordered_set<int64_t> ids;
    for (int i = 0; i < 10; ++i) { ids.insert(gen.next_id()); }

    fake_clock::advance(-50); // wall clock jumps back 50ms
    for (int i = 0; i < 20; ++i) { ids.insert(gen.next_id()); }
    EXPECT_EQ(ids.size(), 30U);
}

TEST_F(rollback_test, drift_rollback_seq_stays_in_reserved_range) {
    basic_generator<fake_clock> gen(options {});
    (void)gen.next_id(); // establish the last_time_tick_ high-water mark

    fake_clock::advance(-20);
    for (int i = 0; i < 4; ++i) {
        const auto parts = gen.decode(gen.next_id());
        EXPECT_GE(parts.seq, 1U) << "rollback compensation seq must stay in the reserved range 1-4";
        EXPECT_LE(parts.seq, 4U);
    }
}

TEST_F(rollback_test, drift_turn_back_tick_decreases_within_round) {
    // within one rollback round: seq stays at the round index while the tick
    // decreases one by one
    const int64_t high_water = reference_now_ms - static_cast<int64_t>(default_base_time);
    basic_generator<fake_clock> gen(options {});
    (void)gen.next_id();

    fake_clock::advance(-100);
    const auto first = gen.decode(gen.next_id());
    const auto second = gen.decode(gen.next_id());
    EXPECT_EQ(first.seq, 1U);
    EXPECT_EQ(second.seq, 1U);
    EXPECT_EQ(second.time_tick, first.time_tick - 1);
    EXPECT_LT(first.time_tick, high_water);
}

TEST_F(rollback_test, drift_turn_back_idx_cycles_modulo_four_across_rounds) {
    // Each round is an independent rollback (idx only bumps at round start):
    // 1,2,3,4,1,2 cycling -- unlimited rollbacks are supported
    basic_generator<fake_clock> gen(options {});
    (void)gen.next_id();

    for (int round = 1; round <= 6; ++round) {
        fake_clock::set(
            reference_now_ms
        ); // clock recovers: crosses the new high water, this rollback round ends
        (void)gen.next_id();
        fake_clock::set(reference_now_ms - 30); // a new rollback round
        const uint32_t expect_idx = (round <= 4) ? round : round - 4;
        EXPECT_EQ(gen.decode(gen.next_id()).seq, expect_idx) << "rollback round " << round;
    }
}

TEST_F(rollback_test, drift_recovers_normal_range_after_clock_catches_up) {
    basic_generator<fake_clock> gen(options {});
    (void)gen.next_id();

    fake_clock::advance(-30);
    for (int i = 0; i < 5; ++i) {
        EXPECT_LE(gen.decode(gen.next_id()).seq, 4U); // still in the rollback compensation phase
    }

    fake_clock::set(reference_now_ms + 10); // clock recovers and crosses the new high-water mark
    const auto after = gen.decode(gen.next_id());
    EXPECT_GE(after.seq, 5U); // back to the normal sequence range
    EXPECT_EQ(after.time_tick, reference_now_ms + 10 - static_cast<int64_t>(default_base_time));
}

TEST_F(rollback_test, drift_rollback_ticks_stay_below_high_water_no_collision) {
    // rollback ids never collide with post-recovery normal ids: compensation ticks walk
    // backwards while normal ticks march forwards
    basic_generator<fake_clock> gen(options {});
    std::unordered_set<int64_t> ids;
    for (int i = 0; i < 3; ++i) { ids.insert(gen.next_id()); }

    fake_clock::advance(-5);
    for (int i = 0; i < 3; ++i) { ids.insert(gen.next_id()); } // compensation
    fake_clock::set(reference_now_ms);                         // back to the original high water
    for (int i = 0; i < 3; ++i) { ids.insert(gen.next_id()); } // normal
    EXPECT_EQ(ids.size(), 9U);
}

TEST_F(rollback_test, original_documented_unsafe_behavior) {
    // the original algorithm ignores rollbacks: no throw, no hang; when the clock returns
    // to a prior tick the sequence restarts at min --
    // this test pins the documented-known-unsafe fact rather than endorsing it.
    // The README warns users:
    // pick algorithm::drift if the clock may jump backwards.
    options o;
    o.algo = algorithm::original;
    basic_generator<fake_clock> gen(o);

    const int64_t first_tick_id = gen.next_id();
    fake_clock::advance(-10);
    EXPECT_NO_THROW((void)gen.next_id());
    EXPECT_GT(first_tick_id, 0);
}
