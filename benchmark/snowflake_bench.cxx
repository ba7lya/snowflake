///
/// @file snowflake_bench.cxx
/// @brief Google Benchmark: next_id throughput and mutex contention.
///
/// All benchmarks use the real system_clock -- a fake clock would only measure the
/// mutex, not the generator itself.
/// The sequence space is widened to 15 bits (32767/ms) so that ordinary throughput
/// rarely hits exhaustion/drift or millisecond waits,
/// keeping both algorithms on their hot path during measurement.
///

#include <benchmark/benchmark.h>
#include <cstdint>
#include <snowflake.hxx>

using ba7lya::snowflake::algorithm;
using ba7lya::snowflake::basic_generator;
using ba7lya::snowflake::options;
using ba7lya::snowflake::system_clock_source;

namespace {

/// Hot-path configuration: a wide sequence avoids exhaustion so the per-iteration cost
/// stays stable
options hot_options(algorithm which) {
    options o;
    o.algo = which;
    o.worker_id_bit_len = 4;
    o.seq_bit_len = 15; // sum = 19 <= 22
    o.worker_id = 1;
    return o;
}

void BM_next_id_single_thread(benchmark::State& state, algorithm which) {
    basic_generator<system_clock_source> gen(hot_options(which));
    for ([[maybe_unused]]
         const auto _ : state) {
        benchmark::DoNotOptimize(gen.next_id());
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_CAPTURE(BM_next_id_single_thread, drift, algorithm::drift);
BENCHMARK_CAPTURE(BM_next_id_single_thread, original, algorithm::original);

void BM_next_id_contended(benchmark::State& state, algorithm which) {
    // one process-wide generator per algo; threads contend on the same mutex
    static basic_generator<system_clock_source> drift_gen(hot_options(algorithm::drift));
    static basic_generator<system_clock_source> original_gen(hot_options(algorithm::original));
    auto& gen = which == algorithm::drift ? drift_gen : original_gen;
    for ([[maybe_unused]]
         const auto _ : state) {
        benchmark::DoNotOptimize(gen.next_id());
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_CAPTURE(BM_next_id_contended, drift, algorithm::drift)
    ->UseRealTime()
    ->Threads(2)
    ->Threads(4)
    ->Threads(8);
BENCHMARK_CAPTURE(BM_next_id_contended, original, algorithm::original)
    ->UseRealTime()
    ->Threads(2)
    ->Threads(4)
    ->Threads(8);

void BM_validate_options(benchmark::State& state) {
    const options opts = hot_options(algorithm::drift);
    for ([[maybe_unused]]
         const auto _ : state) {
        benchmark::DoNotOptimize(ba7lya::snowflake::validate_options(opts, 1778000000000));
    }
}

BENCHMARK(BM_validate_options);

void BM_decode(benchmark::State& state) {
    basic_generator<system_clock_source> gen(hot_options(algorithm::drift));
    const int64_t id = gen.next_id();
    for ([[maybe_unused]]
         const auto _ : state) {
        benchmark::DoNotOptimize(gen.decode(id));
    }
}

BENCHMARK(BM_decode);

} // namespace
