///
/// @file options_tuning.cxx
/// @brief Custom configuration via designated initializers, with exception demos
///

#include <ba7lya/snowflake/snowflake.hxx>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using ba7lya::snowflake::algorithm;
using ba7lya::snowflake::generator;
using ba7lya::snowflake::options;

namespace {

/// Try to build a generator with the given options; print the reason on failure.
/// Returns whether construction succeeded.
bool try_build(const options& opts, const char* label) {
    try {
        generator gen(opts);
        std::cout << "[ok]   " << label << ", first id = " << gen.next_id() << '\n';
        return true;
    }
    catch (const std::invalid_argument& e) {
        std::cout << "[throw] " << label << ": " << e.what() << '\n';
        return false;
    }
}

} // namespace

int main() {
    // custom bit widths: 10-bit worker + 8-bit seq, classic algorithm
    const bool tuned_ok = try_build(
        options {
            .algo = algorithm::original,
            .worker_id = 42,
            .worker_id_bit_len = 10,
            .seq_bit_len = 8,
        },
        "worker 10bit + seq 8bit, original algo"
    );

    // error 1: worker_id exceeds the 2^10-1 cap
    const bool bad_worker = try_build(
        options { .worker_id = 1024, .worker_id_bit_len = 10 },
        "worker_id=1024 with 10 bits (must fail)"
    );

    // error 2: min_seq_num falls into the reserved range 0-4
    const bool bad_min
        = try_build(options { .min_seq_num = 4 }, "min_seq_num=4 in reserved range (must fail)");

    // expectation: the first succeeds, the other two are rejected
    if (!tuned_ok || bad_worker || bad_min) {
        std::cerr << "FAIL: unexpected validation result\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
