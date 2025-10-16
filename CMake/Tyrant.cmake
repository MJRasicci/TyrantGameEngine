include_guard(GLOBAL)

# ---- Core option helper -------------------------------------------------------
# Usage: TGE_OPTION(<variable> <type> <default_value> <docstring> [<allowed_values>...])
macro(TGE_OPTION var type default docstring)
    if("${var}" STREQUAL "" OR "${type}" STREQUAL "" OR "${default}" STREQUAL "" OR "${docstring}" STREQUAL "")
        message(FATAL_ERROR "TGE_OPTION requires: <variable> <type> <default_value> <docstring> [<allowed_values>...]")
    endif()
    if(NOT DEFINED ${var})
        set(${var} "${default}")
    endif()
    # Proper CMake signature: set(VAR VALUE CACHE TYPE DOCSTRING [FORCE])
    set(${var} "${${var}}" CACHE ${type} "${docstring}" FORCE)
    if(${ARGC} GREATER 4)
        set_property(CACHE ${var} PROPERTY STRINGS ${ARGN})
    endif()
endmacro()

# ---- Internals for module registration ---------------------------------------
# Global accumulators (lives in directory scope where included)
set(TGE_SELECTED_OBJECTS "" CACHE INTERNAL "Selected module object files")
set(TGE_SELECTED_MODULES "" CACHE INTERNAL "Selected module object targets")

# Track public include roots for install
set_property(GLOBAL PROPERTY TGE_PUBLIC_INCLUDE_DIRS "")
set(TGE_KNOWN_MODULES "" CACHE INTERNAL "All registered module names")

# Helper: uppercase a name into out-var
function(TGE_UPPER IN OUT)
    string(TOUPPER "${IN}" UPPERED)
    set(${OUT} "${UPPERED}" PARENT_SCOPE)
endfunction()

# Helper: define a cache BOOL if not present
function(TGE_DEFINE_BOOL name default doc)
    if(NOT DEFINED ${name})
        set(${name} "${default}")
    endif()
    set(${name} "${${name}}" CACHE BOOL "${doc}" FORCE)
endfunction()

# ---- Public API: TGE_MODULE ---------------------------------------------------
# Usage:
#   TGE_MODULE(
#     <Name>
#     [REQUIRED]
#     [DEPS dep1 dep2 ...]
#     [INCLUDE_DIR <path>]    # public headers root (default: Runtime/<Name>/include)
#     [SOURCE_DIR  <path>]    # source root (default: Runtime/<Name>/source)
#     [SOURCES ...]           # explicit source list (else glob *.cpp under SOURCE_DIR)
#     [PUBLIC_DEFINES ...]    # extra public defines
#     [PRIVATE_DEFINES ...]   # extra private defines
#   )
function(TGE_MODULE name)
    set(options REQUIRED)
    set(oneValueArgs INCLUDE_DIR SOURCE_DIR)
    set(multiValueArgs DEPS SOURCES PUBLIC_DEFINES PRIVATE_DEFINES)
    cmake_parse_arguments(TGE_MOD "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if("${name}" STREQUAL "")
        message(FATAL_ERROR "TGE_MODULE: missing module name")
    endif()

    if(NOT TGE_MOD_INCLUDE_DIR)
        set(TGE_MOD_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Modules/${name}/include")
    endif()
    if(NOT TGE_MOD_SOURCE_DIR)
        set(TGE_MOD_SOURCE_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/Modules/${name}/source")
    endif()

    TGE_UPPER("${name}" NAME_UP)
    set(_opt "TGE_MODULE_${NAME_UP}")
    TGE_DEFINE_BOOL(${_opt} ON "Build module ${name}")

    if(TGE_MOD_REQUIRED)
        set(${_opt} ON CACHE BOOL "" FORCE)
        set_property(GLOBAL APPEND PROPERTY TGE_REQUIRED_MODULES "${name}")
    endif()

    foreach(dep IN LISTS TGE_MOD_DEPS)
        TGE_UPPER("${dep}" DEP_UP)
        set(_dep_opt "TGE_MODULE_${DEP_UP}")
        if(${_opt})
            TGE_DEFINE_BOOL(${_dep_opt} ON "Build module ${dep} (dependency of ${name})")
            set(${_dep_opt} ON CACHE BOOL "" FORCE)
        else()
            TGE_DEFINE_BOOL(${_dep_opt} OFF "Build module ${dep}")
        endif()
    endforeach()

    set(TGE_KNOWN_MODULES "${TGE_KNOWN_MODULES};${name}" CACHE INTERNAL "" FORCE)
    if(NOT ${_opt})
        message(STATUS "TGE: skipping module ${name} (TGE_MODULE_${NAME_UP}=OFF)")
        return()
    endif()

    # Register this module's public include root for later install
    if(EXISTS "${TGE_MOD_INCLUDE_DIR}")
        # Append to global property (dedup will happen at install time)
        set_property(GLOBAL APPEND PROPERTY TGE_PUBLIC_INCLUDE_DIRS "${TGE_MOD_INCLUDE_DIR}")
    else()
        message(WARNING "TGE_MODULE ${name}: include dir not found: ${TGE_MOD_INCLUDE_DIR}")
    endif()

    set(_srcs "${TGE_MOD_SOURCES}")
    if(_srcs STREQUAL "")
        file(GLOB_RECURSE _srcs CONFIGURE_DEPENDS
             "${TGE_MOD_SOURCE_DIR}/*.cpp" "${TGE_MOD_SOURCE_DIR}/*.cxx" "${TGE_MOD_SOURCE_DIR}/*.c")
    endif()
    if(_srcs STREQUAL "")
        message(FATAL_ERROR "TGE_MODULE ${name}: no sources found (searched in ${TGE_MOD_SOURCE_DIR})")
    endif()

    set(_obj_target "tge_${name}_obj")
    add_library(${_obj_target} OBJECT ${_srcs})
    add_library(TGE::${name}_obj ALIAS ${_obj_target})

    target_compile_features(${_obj_target} PUBLIC cxx_std_23)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${_obj_target} PRIVATE -fvisibility=hidden)
    endif()
    set_property(TARGET ${_obj_target} PROPERTY POSITION_INDEPENDENT_CODE ON)
    target_include_directories(${_obj_target}
        PUBLIC
            $<BUILD_INTERFACE:${TGE_MOD_INCLUDE_DIR}>
            $<INSTALL_INTERFACE:include>)

    if(TGE_MOD_PUBLIC_DEFINES)
        target_compile_definitions(${_obj_target} PUBLIC ${TGE_MOD_PUBLIC_DEFINES})
    endif()
    if(TGE_MOD_PRIVATE_DEFINES)
        target_compile_definitions(${_obj_target} PRIVATE ${TGE_MOD_PRIVATE_DEFINES})
    endif()

    foreach(dep IN LISTS TGE_MOD_DEPS)
        target_link_libraries(${_obj_target} PUBLIC TGE::${dep}_obj)
    endforeach()

    set(TGE_SELECTED_OBJECTS "${TGE_SELECTED_OBJECTS};$<TARGET_OBJECTS:${_obj_target}>" CACHE INTERNAL "" FORCE)
    set(TGE_SELECTED_MODULES "${TGE_SELECTED_MODULES};TGE::${name}_obj" CACHE INTERNAL "" FORCE)
endfunction()

# ---- Validate requiredness at the end of module registration ------------------
# Call after declaring all modules to enforce user misconfiguration.
function(TGE_VALIDATE_REQUIRED_MODULES)
    get_property(_req GLOBAL PROPERTY TGE_REQUIRED_MODULES)
    if(NOT _req)
        return()
    endif()
    foreach(r IN LISTS _req)
        TGE_UPPER("${r}" R_UP)
        if(NOT TGE_MODULE_${R_UP})
            message(FATAL_ERROR "Module '${r}' is REQUIRED but was disabled. Enable -DTGE_MODULE_${R_UP}=ON.")
        endif()
    endforeach()
endfunction()

# Function to declare test modules
function(TGE_TEST_MODULE module)
    if("${module}" STREQUAL "")
        message(FATAL_ERROR "TGE_TEST_MODULE: missing module name")
    endif()

    set(module_dir "${CMAKE_CURRENT_SOURCE_DIR}/${module}")
    if(NOT IS_DIRECTORY "${module_dir}")
        message(FATAL_ERROR "TGE_TEST_MODULE ${module}: directory not found at ${module_dir}")
    endif()

    set(unit_dir "${module_dir}/Unit")
    set(benchmark_dir "${module_dir}/Benchmarks")

    set(unit_sources)
    if(IS_DIRECTORY "${unit_dir}")
        file(GLOB unit_sources CONFIGURE_DEPENDS "${unit_dir}/*.cpp")
    endif()

    set(benchmark_sources)
    if(IS_DIRECTORY "${benchmark_dir}")
        file(GLOB benchmark_sources CONFIGURE_DEPENDS "${benchmark_dir}/*.cpp")
    endif()

    if(unit_sources)
        if(NOT TGE_ENABLE_TESTS)
            message(STATUS "Skipping unit tests for module ${module}; GoogleTest not available.")
        else()
            set(test_target "tge_test_${module}")
            add_executable(${test_target} ${unit_sources})
            target_compile_features(${test_target} PRIVATE cxx_std_23)
            target_link_libraries(${test_target} PRIVATE TGE::runtime GTest::gmock_main)
            set_target_properties(${test_target} PROPERTIES FOLDER "Tests/${module}")

            if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
                set_target_properties(${test_target} PROPERTIES
                    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
            endif()

            include(GoogleTest) # safe if repeated
            gtest_discover_tests(${test_target}
            TEST_PREFIX "${module}."
            DISCOVERY_MODE PRE_TEST
            WORKING_DIRECTORY $<TARGET_FILE_DIR:${test_target}>)
    endif()
    else()
        message(WARNING "TGE_TEST_MODULE ${module}: no unit test sources found under ${unit_dir}")
    endif()

    if(benchmark_sources)
        if(NOT TGE_ENABLE_BENCHMARKS)
            message(STATUS "Skipping benchmarks for module ${module}; Google Benchmark not available.")
        else()
            set(benchmark_target "tge_bench_${module}")
            add_executable(${benchmark_target} ${benchmark_sources})
            target_compile_features(${benchmark_target} PRIVATE cxx_std_23)
            target_link_libraries(${benchmark_target} PRIVATE TGE::runtime benchmark::benchmark_main)
            set_target_properties(${benchmark_target} PROPERTIES FOLDER "Tests/${module}")

            if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
                set_target_properties(${benchmark_target} PROPERTIES
                    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
            endif()

            add_test(NAME "${module}.Benchmarks"
                    COMMAND ${benchmark_target} --benchmark_format=console
                    WORKING_DIRECTORY $<TARGET_FILE_DIR:${benchmark_target}>)
        endif()
    endif()

    if(NOT unit_sources AND NOT benchmark_sources)
        message(WARNING "TGE_TEST_MODULE ${module}: no test or benchmark sources found under ${module_dir}")
    endif()
endfunction()