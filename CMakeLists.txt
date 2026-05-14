# --- cmake version ---
cmake_minimum_required(VERSION 3.21)

# --- project ---
project(ba7lya.xxxx LANGUAGES CXX)

# --- build options ---
option(${PROJECT_NAME}_BUILD_BENCHMARK "Build benchmarks"    OFF)
option(${PROJECT_NAME}_BUILD_EXAMPLE   "Build examples"      OFF)
option(${PROJECT_NAME}_BUILD_DOC       "Build Documentation" OFF)
option(${PROJECT_NAME}_BUILD_TEST      "Build Test"          OFF)

# --- includes ---
include(CTest)
include(GNUInstallDirs)
include(GoogleTest)

# --- dependencies ---
find_package(benchmark CONFIG REQUIRED)
find_package(GTest     CONFIG REQUIRED)
find_package(spdlog    CONFIG REQUIRED)

# --- os ---
if (WIN32)
    add_definitions(-DWINVER=0x0A00)       # Windows 10
    add_definitions(-D_WIN32_WINNT=0x0A00) # Windows 10
    add_definitions(-DNOMINMAX)            # disable C2589 min()/max() error
endif()

# --- modules ---
add_subdirectory(thirdparty)

# --- target ---
add_library(${PROJECT_NAME})
add_library(ba7lya::xxxx ALIAS ${PROJECT_NAME})

# --- include directory ---
target_include_directories(
    ${PROJECT_NAME}
    PUBLIC
    ${PROJECT_SOURCE_DIR}/include/
)

# --- source files ---
target_sources(
    ${PROJECT_NAME}
    PRIVATE
    src/app.cxx
)

# --- linakge ---
target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
    spdlog::spdlog
)

# --- compile features ---
target_compile_features(
    ${PROJECT_NAME}
    PRIVATE
    cxx_std_23
)

# --- compile options ---
target_compile_options(
    ${PROJECT_NAME}
    PRIVATE
    "$<$<C_COMPILER_ID:MSVC>:/utf-8>"
    "$<$<CXX_COMPILER_ID:MSVC>:/utf-8>"
)

# --- benchmark ---
if(${PROJECT_NAME}_BUILD_BENCHMARK)
    add_subdirectory(benchmark)
endif()

# --- examples ---
if(${PROJECT_NAME}_BUILD_EXAMPLE)
    add_subdirectory(examples)
endif()

# --- unit tests ---
if(${PROJECT_NAME}_BUILD_TEST)
    enable_testing()
    add_subdirectory(test)
endif()
