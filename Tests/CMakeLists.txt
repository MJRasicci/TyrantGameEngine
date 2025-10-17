cmake_minimum_required(VERSION 3.25)

find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND OR NOT TARGET GTest::gmock_main)
    message(STATUS "GoogleTest package not available; fetching from source.")
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(googletest)
endif()

if(TARGET GTest::gmock_main)
    include(GoogleTest)
    set(TGE_ENABLE_TESTS ON)
else()
    message(STATUS "GoogleTest not found; unit tests will be skipped.")
    set(TGE_ENABLE_TESTS OFF)
endif()

find_package(benchmark CONFIG QUIET)
if(NOT benchmark_FOUND)
    message(STATUS "Google Benchmark package not available; fetching from source.")
    include(FetchContent)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.8.3
        GIT_SHALLOW TRUE)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)
endif()

if(TARGET benchmark::benchmark)
    set(TGE_ENABLE_BENCHMARKS ON)
else()
    message(STATUS "Google Benchmark not found; benchmark targets will be skipped.")
    set(TGE_ENABLE_BENCHMARKS OFF)
endif()

# Register runtime module test suites here
TGE_TEST_MODULE(Core)
