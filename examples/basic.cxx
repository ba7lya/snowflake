///
/// @file basic.cxx
/// @brief Generate 10 snowflake ids with default options and print the decoded fields
///

#include <ba7lya/snowflake/snowflake.hxx>
#include <cstdlib>
#include <iostream>

int main() {
    ba7lya::snowflake::generator gen { ba7lya::snowflake::options { .worker_id = 1 } };

    for (int i = 0; i < 10; ++i) {
        const int64_t id = gen.next_id();
        const auto parts = gen.decode(id);
        std::cout << "id=" << id << "  tick=" << parts.time_tick << "ms  worker=" << parts.worker_id
                  << "  seq=" << parts.seq << '\n';
    }
    return EXIT_SUCCESS;
}
