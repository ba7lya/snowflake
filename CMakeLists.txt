# --- cmake version ---
cmake_minimum_required(VERSION 3.21)

# --- project ---
project(ba7lya.snowflake VERSION 0.2.0 LANGUAGES CXX)

# --- build options ---
option(SNOWFLAKE_BUILD_BENCHMARK "Build benchmarks" OFF)
option(SNOWFLAKE_BUILD_EXAMPLES  "Build examples"   OFF)
option(SNOWFLAKE_BUILD_TESTS     "Build tests"      OFF)

# --- includes ---
include(GNUInstallDirs)

# --- predictable output layout: all executables under <build>/bin[/<Config>] ---
if(CMAKE_CONFIGURATION_TYPES)
    foreach(cfg IN LISTS CMAKE_CONFIGURATION_TYPES)
        string(TOUPPER ${cfg} cfg_upper)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_upper} "${CMAKE_BINARY_DIR}/bin/${cfg}")
    endforeach()
else()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

# --- target: header-only library ---
add_library(${PROJECT_NAME} INTERFACE)
add_library(ba7lya::snowflake ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} INTERFACE
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_20)
target_compile_options(${PROJECT_NAME} INTERFACE
    "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic>")

# --- install & export ---
# Source tree: the single header lives at include/snowflake.hxx and in-tree consumers
# include it as <snowflake.hxx>. As a dependency library it installs to
# <ba7lya/snowflake/snowflake.hxx> (see the install(FILES) below).
install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}Targets)
install(FILES include/snowflake.hxx DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ba7lya/snowflake)
install(EXPORT ${PROJECT_NAME}Targets
    FILE ${PROJECT_NAME}Config.cmake
    NAMESPACE ba7lya::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

# --- license ---
if(EXISTS "${PROJECT_SOURCE_DIR}/LICENSE")
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME})
endif()

# --- benchmark ---
if(SNOWFLAKE_BUILD_BENCHMARK)
    add_subdirectory(benchmark)
endif()

# --- examples ---
if(SNOWFLAKE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# --- unit tests ---
if(SNOWFLAKE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
