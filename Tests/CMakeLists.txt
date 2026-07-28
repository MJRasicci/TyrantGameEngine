cmake_minimum_required(VERSION 3.30)

find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND OR NOT TARGET GTest::gmock_main)
    message(STATUS "GoogleTest package not available; fetching from source.")
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
        GIT_SHALLOW TRUE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
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
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
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
if(TGE_MODULE_RENDERING)
    TGE_TEST_MODULE(Rendering)
endif()
if(TGE_MODULE_DRAWING2D)
    TGE_TEST_MODULE(Drawing2D)
endif()
if(TGE_MODULE_WORLD2D)
    TGE_TEST_MODULE(World2D)
endif()
TGE_TEST_MODULE(Application)
TGE_TEST_MODULE(CliApplication)
TGE_TEST_MODULE(DesktopEventRuntime)
TGE_TEST_MODULE(Graphics)
TGE_TEST_MODULE(Input)
TGE_TEST_MODULE(GuiApplication)
if(TGE_MODULE_SDLDESKTOP)
    TGE_TEST_MODULE(SDLDesktop)
endif()

add_test(
    NAME Editor.HeadlessSmoke
    COMMAND $<TARGET_FILE:tge_editor> --headless)
set_tests_properties(
    Editor.HeadlessSmoke
    PROPERTIES
        LABELS "Editor"
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:tge_editor>")

if(TGE_MODULE_SDLDESKTOP)
    add_test(
        NAME Editor.SDLDesktopSmoke
        COMMAND $<TARGET_FILE:tge_editor> --smoke)
    set_tests_properties(
        Editor.SDLDesktopSmoke
        PROPERTIES
            LABELS "Editor;SDL"
            RUN_SERIAL TRUE
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:tge_editor>"
            ENVIRONMENT "SDL_VIDEODRIVER=dummy")
endif()

# Exercise the installed public surface from a clean downstream project. Cross
# builds cannot generally execute their produced consumer on the build host.
if(NOT CMAKE_CROSSCOMPILING)
    configure_file(
        RunInstalledConsumer.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/RunInstalledConsumer.cmake"
        @ONLY)
    add_test(
        NAME Package.InstalledConsumer
        COMMAND
            "${CMAKE_COMMAND}"
            "-DTGE_TEST_CONFIG=$<CONFIG>"
            -P "${CMAKE_CURRENT_BINARY_DIR}/RunInstalledConsumer.cmake")
    set_tests_properties(
        Package.InstalledConsumer
        PROPERTIES
            LABELS "Package"
            RUN_SERIAL TRUE)
endif()
