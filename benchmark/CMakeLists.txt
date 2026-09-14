# --- benchmark ---
find_package(benchmark CONFIG REQUIRED)

add_executable(snowflake_bench snowflake_bench.cxx)
target_compile_features(snowflake_bench PRIVATE cxx_std_20)
target_link_libraries(snowflake_bench PRIVATE
    ba7lya::snowflake
    benchmark::benchmark
    benchmark::benchmark_main)
