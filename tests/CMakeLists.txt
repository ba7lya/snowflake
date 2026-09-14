find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(snowflake_tests
    options_test.cxx
    layout_test.cxx
    uniqueness_test.cxx
    rollback_test.cxx
    sequence_test.cxx
    worker_test.cxx)

target_include_directories(snowflake_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(snowflake_tests PRIVATE cxx_std_20)
target_link_libraries(snowflake_tests PRIVATE
    ba7lya::snowflake
    GTest::gtest
    GTest::gtest_main)

gtest_discover_tests(snowflake_tests DISCOVERY_MODE PRE_TEST)
