cmake_minimum_required(VERSION 3.25)

find_package(GTest CONFIG QUIET)
if(GTest_FOUND)
    include(GoogleTest)
    set(TGE_ENABLE_TESTS ON)
else()
    message(STATUS "GoogleTest not found; unit tests will be skipped.")
    set(TGE_ENABLE_TESTS OFF)
endif()

find_package(benchmark CONFIG QUIET)
if(benchmark_FOUND)
    set(TGE_ENABLE_BENCHMARKS ON)
else()
    message(STATUS "Google Benchmark not found; benchmark targets will be skipped.")
    set(TGE_ENABLE_BENCHMARKS OFF)
endif()

# Register runtime module test suites here
TGE_TEST_MODULE(Core)
