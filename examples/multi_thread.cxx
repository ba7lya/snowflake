///
/// @file multi_thread.cxx
/// @brief Four threads share one generator; merged results must be unique
///

#include <cstdlib>
#include <iostream>
#include <snowflake.hxx>
#include <thread>
#include <unordered_set>
#include <vector>

int main() {
    ba7lya::snowflake::generator gen { ba7lya::snowflake::options { .worker_id = 2 } };

    constexpr int thread_count = 4;
    constexpr int per_thread = 100000;

    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    std::vector<std::vector<int64_t>> results(thread_count);
    for (auto& out : results) {
        threads.emplace_back(
            [&gen, &out]
            {
                for (int i = 0; i < per_thread; ++i) { out.push_back(gen.next_id()); }
            }
        );
    }
    for (auto& th : threads) { th.join(); }

    std::unordered_set<int64_t> all;
    for (const auto& part : results) { all.insert(part.begin(), part.end()); }

    const auto expected = static_cast<std::size_t>(thread_count) * per_thread;
    std::cout << "generated " << expected << " ids, unique " << all.size() << '\n';
    if (all.size() != expected) {
        std::cerr << "FAIL: duplicate ids detected\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
