///
/// @file worker_test.cxx
/// @brief Distinct workers produce disjoint id spaces + strict per-worker monotonicity
///

#include <ba7lya/snowflake/snowflake.hxx>
#include <cstdint>
#include <gtest/gtest.h>
#include <unordered_set>

#include "fake_clock.hxx"

using namespace ba7lya::snowflake;
using snowflake_tests::fake_clock;

namespace {

constexpr int64_t reference_now_ms = 1778000000000;

std::unordered_set<uint32_t> collect_worker_bits(generator& gen, std::size_t count) {
    std::unordered_set<uint32_t> workers;
    for (std::size_t i = 0; i < count; ++i) { workers.insert(gen.decode(gen.next_id()).worker_id); }
    return workers;
}

} // namespace

TEST(worker_test, distinct_worker_ids_produce_disjoint_id_spaces) {
    options o1;
    o1.worker_id = 1;
    options o2;
    o2.worker_id = 2;
    o2.max_seq_num = 0; // defaults to 63

    generator g1(o1);
    generator g2(o2);

    std::unordered_set<int64_t> ids1;
    std::unordered_set<int64_t> ids2;
    for (int i = 0; i < 10000; ++i) {
        ids1.insert(g1.next_id());
        ids2.insert(g2.next_id());
    }

    // different worker bits => the id spaces are necessarily disjoint
    for (const int64_t id : ids1) { EXPECT_EQ(ids2.count(id), 0U); }
    EXPECT_EQ(collect_worker_bits(g1, 100).count(1U), 1U);
    EXPECT_EQ(collect_worker_bits(g2, 100).count(2U), 1U);
}

TEST(worker_test, same_worker_same_tick_different_workers) {
    // under the fake clock both workers share tick/seq exactly; only worker bits differ
    fake_clock::set(reference_now_ms);
    options base;
    base.seq_bit_len = 6;
    base.worker_id_bit_len = 6;

    options oa = base;
    oa.worker_id = 10;
    options ob = base;
    ob.worker_id = 20;

    basic_generator<fake_clock> ga(oa);
    basic_generator<fake_clock> gb(ob);

    const auto pa = ga.decode(ga.next_id());
    const auto pb = gb.decode(gb.next_id());
    EXPECT_EQ(pa.time_tick, pb.time_tick);
    EXPECT_EQ(pa.seq, pb.seq);
    EXPECT_NE(pa.worker_id, pb.worker_id);
    EXPECT_NE(ga.next_id(), gb.next_id());
}

class monotonic_test : public ::testing::TestWithParam<algo> {};

TEST_P(monotonic_test, strictly_increasing_within_one_worker) {
    options o;
    o.algo = GetParam();
    o.worker_id = 3;
    generator gen(o);

    int64_t prev = gen.next_id();
    for (int i = 0; i < 10000; ++i) {
        const int64_t next = gen.next_id();
        EXPECT_GT(next, prev) << "id stopped increasing strictly at iteration " << i;
        prev = next;
    }
}

INSTANTIATE_TEST_SUITE_P(algos, monotonic_test, ::testing::Values(algo::drift, algo::original));
