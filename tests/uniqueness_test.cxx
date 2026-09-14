///
/// @file uniqueness_test.cxx
/// @brief Single-/multi-thread uniqueness -- real clock, so sequence exhaustion and the
/// drift path actually happen
///

#include <ba7lya/snowflake/snowflake.hxx>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <thread>
#include <unordered_set>
#include <vector>

using ba7lya::snowflake::algo;
using ba7lya::snowflake::generator;
using ba7lya::snowflake::options;

namespace {

void collect_unique(generator& gen, std::unordered_set<int64_t>& out, std::size_t count) {
    out.reserve(out.size() + count);
    for (std::size_t i = 0; i < count; ++i) { out.insert(gen.next_id()); }
}

} // namespace

class uniqueness_test : public ::testing::TestWithParam<algo> {};

TEST_P(uniqueness_test, single_thread_one_million) {
    options o;
    o.algo = GetParam();
    o.worker_id = 7;
    o.seq_bit_len = 12; // ~4091 sequences per ms, 1e6 ids take about 0.25s of real time
    generator gen(o);

    std::unordered_set<int64_t> ids;
    collect_unique(gen, ids, 1000000);
    EXPECT_EQ(ids.size(), 1000000U);
}

TEST_P(uniqueness_test, eight_threads_share_one_generator) {
    options o;
    o.algo = GetParam();
    o.worker_id = 1;
    o.seq_bit_len = 12;
    generator gen(o);

    constexpr int thread_count = 8;
    constexpr std::size_t per_thread = 100000;

    std::vector<std::unordered_set<int64_t>> buckets(thread_count);
    std::vector<std::jthread> threads;
    threads.reserve(buckets.size());
    for (auto& bucket : buckets) {
        threads.emplace_back([&gen, &bucket] { collect_unique(gen, bucket, per_thread); });
    }
    for (auto& th : threads) {
        th.join();
    } // assert must run after the workers finish;
      // jthread's dtor join would be too late

    std::unordered_set<int64_t> all;
    for (const auto& bucket : buckets) { all.merge(std::unordered_set<int64_t>(bucket)); }
    EXPECT_EQ(all.size(), static_cast<std::size_t>(thread_count) * per_thread);
}

INSTANTIATE_TEST_SUITE_P(algos, uniqueness_test, ::testing::Values(algo::drift, algo::original));
