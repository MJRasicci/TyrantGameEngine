cmake_minimum_required(VERSION 3.30)
project(TyrantGameEngine VERSION 2.0.0.0 LANGUAGES C CXX)
include(./CMake/Tyrant.cmake)
include(CheckCXXSourceCompiles)
include(CMakePackageConfigHelpers)
include(FetchContent)
include(GNUInstallDirs)

find_package(Threads REQUIRED)

# Define version numbers
set(TGE_VERSION_MAJOR ${CMAKE_PROJECT_VERSION_MAJOR})
set(TGE_VERSION_MINOR ${CMAKE_PROJECT_VERSION_MINOR})
set(TGE_VERSION_PATCH ${CMAKE_PROJECT_VERSION_PATCH})
set(TGE_PACKAGE_INSTALL_DIR
    "${CMAKE_INSTALL_LIBDIR}/cmake/TyrantGameEngine")

add_definitions(-DTGE_VERSION_MAJOR=${TGE_VERSION_MAJOR})
add_definitions(-DTGE_VERSION_MINOR=${TGE_VERSION_MINOR})
add_definitions(-DTGE_VERSION_PATCH=${TGE_VERSION_PATCH})

# Configure CPack
set(CPACK_PACKAGE_VERSION_MAJOR ${CMAKE_PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${CMAKE_PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${CMAKE_PROJECT_VERSION_PATCH})

# TGE intentionally targets the current C++ standard. Individual C++26
# facilities are still probed below because compiler and standard-library
# implementations ship them independently.
set(TGE_CXX_STANDARD 26)
set(TGE_CMAKE_CXX_STANDARD ${TGE_CXX_STANDARD})

# MSVC exposes its evolving C++26 implementation through /std:c++latest.
# Until CMake models that as a distinct C++26 level, requesting cxx_std_26
# fails during generation before TGE's capability probes can run.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND
   CMAKE_CXX_STANDARD_LATEST LESS TGE_CXX_STANDARD)
  set(TGE_CMAKE_CXX_STANDARD 23)
endif()

set(CMAKE_CXX_STANDARD ${TGE_CMAKE_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable folders in IDEs to organize targets
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Compiler Settings
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options(-fvisibility=hidden)
endif()
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# Options
TGE_OPTION(TGE_BUILD_SHARED BOOL FALSE "TRUE to build the runtime as a shared library, FALSE to build the runtime for static linkage")
TGE_OPTION(TGE_BUILD_DOCS BOOL FALSE "TRUE to build the documentation, FALSE to not (requires doxygen)")
TGE_OPTION(TGE_BUILD_TESTS BOOL FALSE "TRUE to enable building the test suite")
TGE_OPTION(
  TGE_MODULE_SDLDESKTOP
  BOOL
  TRUE
  "TRUE to build the private SDL desktop window and input backend")
TGE_OPTION(
  TGE_REFLECTION_DI
  STRING
  AUTO
  "C++26 reflection-generated service activation: AUTO, ON, or OFF"
  AUTO ON OFF)
TGE_OPTION(
  TGE_OPTIONS_REFLECTION
  STRING
  AUTO
  "C++26 reflection-generated options serialization: AUTO, ON, or OFF"
  AUTO ON OFF)
TGE_OPTION(
  TGE_EXECUTION_BACKEND
  STRING
  AUTO
  "Asynchronous execution backend: AUTO, NATIVE, or STDEXEC"
  AUTO NATIVE STDEXEC)

string(TOUPPER "${TGE_REFLECTION_DI}" TGE_REFLECTION_DI)
set(TGE_REFLECTION_DI "${TGE_REFLECTION_DI}" CACHE STRING
    "C++26 reflection-generated service activation: AUTO, ON, or OFF" FORCE)
set_property(CACHE TGE_REFLECTION_DI PROPERTY STRINGS AUTO ON OFF)

if(NOT TGE_REFLECTION_DI MATCHES "^(AUTO|ON|OFF)$")
  message(FATAL_ERROR
    "TGE_REFLECTION_DI must be AUTO, ON, or OFF (received '${TGE_REFLECTION_DI}').")
endif()

string(TOUPPER "${TGE_OPTIONS_REFLECTION}" TGE_OPTIONS_REFLECTION)
set(TGE_OPTIONS_REFLECTION "${TGE_OPTIONS_REFLECTION}" CACHE STRING
    "C++26 reflection-generated options serialization: AUTO, ON, or OFF" FORCE)
set_property(CACHE TGE_OPTIONS_REFLECTION PROPERTY STRINGS AUTO ON OFF)

if(NOT TGE_OPTIONS_REFLECTION MATCHES "^(AUTO|ON|OFF)$")
  message(FATAL_ERROR
    "TGE_OPTIONS_REFLECTION must be AUTO, ON, or OFF "
    "(received '${TGE_OPTIONS_REFLECTION}').")
endif()

string(TOUPPER "${TGE_EXECUTION_BACKEND}" TGE_EXECUTION_BACKEND)
set(TGE_EXECUTION_BACKEND "${TGE_EXECUTION_BACKEND}" CACHE STRING
    "Asynchronous execution backend: AUTO, NATIVE, or STDEXEC" FORCE)
set_property(CACHE TGE_EXECUTION_BACKEND PROPERTY STRINGS AUTO NATIVE STDEXEC)

if(NOT TGE_EXECUTION_BACKEND MATCHES "^(AUTO|NATIVE|STDEXEC)$")
  message(FATAL_ERROR
    "TGE_EXECUTION_BACKEND must be AUTO, NATIVE, or STDEXEC "
    "(received '${TGE_EXECUTION_BACKEND}').")
endif()

set(_tge_reflection_di_supported FALSE)

if(NOT TGE_REFLECTION_DI STREQUAL "OFF")
  set(_tge_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")

  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    string(APPEND CMAKE_REQUIRED_FLAGS " -freflection")
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    string(APPEND CMAKE_REQUIRED_FLAGS
      " -freflection -fexpansion-statements")
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    string(APPEND CMAKE_REQUIRED_FLAGS " -freflection")
  endif()

  # Compiler changes in an existing build tree must never retain a stale probe.
  unset(TGE_COMPILER_HAS_CXX26_REFLECTION_DI CACHE)
  check_cxx_source_compiles([=[
    #include <concepts>
    #include <memory>
    #include <meta>

    #if !defined(__cpp_impl_reflection) || __cpp_impl_reflection < 202603L
      #error "The required C++26 reflection language support is unavailable."
    #endif

    #if !defined(__cpp_lib_reflection) || __cpp_lib_reflection < 202603L
      #error "The required C++26 reflection library support is unavailable."
    #endif

    struct Dependency {};
    struct InjectAttribute {};
    inline constexpr InjectAttribute Inject {};

    struct Service
    {
        Service() = default;

        [[=Inject]]
        explicit Service(std::shared_ptr<Dependency>) {}
    };

    consteval std::meta::info injectable_constructor()
    {
        for (auto member :
             std::meta::members_of(
                 ^^Service, std::meta::access_context::unprivileged()))
        {
            if (std::meta::is_constructor(member) &&
                !std::meta::annotations_of_with_type(
                    member, ^^InjectAttribute).empty())
            {
                return member;
            }
        }

        return {};
    }

    constexpr auto Constructor = injectable_constructor();
    static_assert(Constructor != std::meta::info {});
    static_assert(std::meta::parameters_of(Constructor).size() == 1);

    using Parameter = typename [:std::meta::type_of(
        std::meta::parameters_of(Constructor)[0]):];
    static_assert(std::same_as<Parameter, std::shared_ptr<Dependency>>);

    int main() {}
  ]=] TGE_COMPILER_HAS_CXX26_REFLECTION_DI)

  set(CMAKE_REQUIRED_FLAGS "${_tge_saved_required_flags}")

  if(TGE_COMPILER_HAS_CXX26_REFLECTION_DI)
    set(_tge_reflection_di_supported TRUE)
  endif()
endif()

if(TGE_REFLECTION_DI STREQUAL "ON" AND NOT _tge_reflection_di_supported)
  message(FATAL_ERROR
    "TGE_REFLECTION_DI=ON requires a compiler and standard library with "
    "the C++26 reflection facilities used by TGE.")
endif()

if(_tge_reflection_di_supported AND NOT TGE_REFLECTION_DI STREQUAL "OFF")
  set(TGE_REFLECTION_DI_ENABLED TRUE)
  message(STATUS
    "TGE: enabling C++26 reflection-generated dependency injection")
else()
  set(TGE_REFLECTION_DI_ENABLED FALSE)
  message(STATUS
    "TGE: using portable traits-based dependency injection")
endif()

# Prefer the standard C++26 execution facilities when the active standard
# library implements both senders and the coroutine task type.
set(_tge_native_execution_supported FALSE)

if(NOT TGE_EXECUTION_BACKEND STREQUAL "STDEXEC")
  unset(TGE_COMPILER_HAS_NATIVE_CXX26_EXECUTION CACHE)
  set(_tge_saved_required_libraries "${CMAKE_REQUIRED_LIBRARIES}")
  set(CMAKE_REQUIRED_LIBRARIES Threads::Threads)

  check_cxx_source_compiles([=[
    #include <exception>
    #include <execution>
    #include <stdexcept>
    #include <thread>
    #include <tuple>
    #include <version>

    #if !defined(__cpp_lib_senders) || __cpp_lib_senders < 202506L
      #error "C++26 senders are unavailable."
    #endif

    #if !defined(__cpp_lib_task) || __cpp_lib_task < 202506L
      #error "C++26 execution::task is unavailable."
    #endif

    std::execution::task<int> Answer()
    {
        co_return 42;
    }

    int main()
    {
        auto operation = std::execution::stopped_as_error(
            Answer(),
            std::make_exception_ptr(std::runtime_error("stopped")));
        auto result = std::this_thread::sync_wait(std::move(operation));
        return result && std::get<0>(*result) == 42 ? 0 : 1;
    }
  ]=] TGE_COMPILER_HAS_NATIVE_CXX26_EXECUTION)

  set(CMAKE_REQUIRED_LIBRARIES "${_tge_saved_required_libraries}")

  if(TGE_COMPILER_HAS_NATIVE_CXX26_EXECUTION)
    set(_tge_native_execution_supported TRUE)
  endif()
endif()

if(TGE_EXECUTION_BACKEND STREQUAL "NATIVE" AND
   NOT _tge_native_execution_supported)
  message(FATAL_ERROR
    "TGE_EXECUTION_BACKEND=NATIVE requires C++26 std::execution senders, "
    "std::execution::task, and std::this_thread::sync_wait.")
endif()

add_library(tge_execution_backend INTERFACE)
add_library(TGE::execution_backend ALIAS tge_execution_backend)
target_link_libraries(tge_execution_backend INTERFACE Threads::Threads)

if(_tge_native_execution_supported AND
   NOT TGE_EXECUTION_BACKEND STREQUAL "STDEXEC")
  set(TGE_NATIVE_EXECUTION_ENABLED TRUE)
  message(STATUS "TGE: using native C++26 std::execution")
else()
  set(TGE_NATIVE_EXECUTION_ENABLED FALSE)

  # stdexec is a header-only reference implementation of the C++26 execution
  # model. SOURCE_SUBDIR deliberately names a non-project directory so TGE can
  # consume the headers without configuring stdexec's development dependencies.
  FetchContent_Declare(
    tge_stdexec
    URL
      https://github.com/NVIDIA/stdexec/archive/f7b08650de40b9789a1b9f45df37d7d0a8194f6d.tar.gz
    URL_HASH
      SHA256=b9b442a5c4482f69838fe1f18cbc79092487abf31093e76165ddcb9efaf29882
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR __tge_header_only)
  FetchContent_MakeAvailable(tge_stdexec)

  set(_tge_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
  set(_tge_saved_required_includes "${CMAKE_REQUIRED_INCLUDES}")
  set(_tge_saved_required_libraries "${CMAKE_REQUIRED_LIBRARIES}")
  list(APPEND CMAKE_REQUIRED_INCLUDES "${tge_stdexec_SOURCE_DIR}/include")
  set(CMAKE_REQUIRED_LIBRARIES Threads::Threads)

  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    string(APPEND CMAKE_REQUIRED_FLAGS
      " -fcoroutines -fconcepts-diagnostics-depth=10")
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    string(APPEND CMAKE_REQUIRED_FLAGS
      " /Zc:__cplusplus /Zc:preprocessor /Zc:externConstexpr")
  endif()

  # Validate the compatibility path during configuration so unsupported
  # bleeding-edge toolchain combinations fail with a focused diagnostic.
  unset(TGE_COMPILER_HAS_STDEXEC_EXECUTION CACHE)
  check_cxx_source_compiles([=[
    #include <stdexec/execution.hpp>
    #include <tuple>

    stdexec::task<int> Answer()
    {
        co_return 42;
    }

    int main()
    {
        auto result = stdexec::sync_wait(Answer());
        return result && std::get<0>(*result) == 42 ? 0 : 1;
    }
  ]=] TGE_COMPILER_HAS_STDEXEC_EXECUTION)

  set(CMAKE_REQUIRED_FLAGS "${_tge_saved_required_flags}")
  set(CMAKE_REQUIRED_INCLUDES "${_tge_saved_required_includes}")
  set(CMAKE_REQUIRED_LIBRARIES "${_tge_saved_required_libraries}")

  if(NOT TGE_COMPILER_HAS_STDEXEC_EXECUTION)
    message(FATAL_ERROR
      "The active C++26 toolchain provides neither TGE's required native "
      "std::execution facilities nor a compatible stdexec implementation.")
  endif()

  target_include_directories(
    tge_execution_backend
    SYSTEM INTERFACE
      $<BUILD_INTERFACE:${tge_stdexec_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_compile_options(
    tge_execution_backend
    INTERFACE
      $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fcoroutines;-fconcepts-diagnostics-depth=10>
      $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/Zc:__cplusplus;/Zc:preprocessor;/Zc:externConstexpr;/bigobj>)

  set(TGE_STDEXEC_SOURCE_DIR "${tge_stdexec_SOURCE_DIR}")
  message(STATUS "TGE: using pinned stdexec compatibility backend")
endif()

# Glaze provides the portable, allocation-conscious serialization backend for
# typed runtime options. Fetch only its headers so embedding TGE never mutates
# a superproject's Glaze cache settings or target namespace.
FetchContent_Declare(
  tge_glaze
  URL
    https://github.com/stephenberry/glaze/archive/refs/tags/v7.9.1.tar.gz
  URL_HASH
    SHA256=d6dee391276f5375672c35d06058e4fd8f1f30f62bae163a004b3bd13a4e2ae3
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR __tge_header_only)
FetchContent_MakeAvailable(tge_glaze)
set(TGE_GLAZE_SOURCE_DIR "${tge_glaze_SOURCE_DIR}")

add_library(tge_glaze_headers INTERFACE)
add_library(TGE::glaze_headers ALIAS tge_glaze_headers)
target_compile_features(tge_glaze_headers INTERFACE cxx_std_23)
target_include_directories(
  tge_glaze_headers
  SYSTEM INTERFACE
    $<BUILD_INTERFACE:${tge_glaze_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_options(
  tge_glaze_headers
  INTERFACE
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:/Zc:preprocessor;/permissive-;/Zc:lambda>)

# Use Glaze's direct C++26 field reflection only when this exact integration
# compiles. Other supported toolchains retain Glaze's portable aggregate path.
set(TGE_OPTIONS_REFLECTION_ENABLED FALSE)

if(NOT TGE_OPTIONS_REFLECTION STREQUAL "OFF")
  set(_tge_saved_required_definitions "${CMAKE_REQUIRED_DEFINITIONS}")
  set(_tge_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
  set(_tge_saved_required_includes "${CMAKE_REQUIRED_INCLUDES}")
  list(APPEND CMAKE_REQUIRED_DEFINITIONS -DGLZ_REFLECTION26=1)
  list(APPEND CMAKE_REQUIRED_INCLUDES "${tge_glaze_SOURCE_DIR}/include")

  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    string(APPEND CMAKE_REQUIRED_FLAGS " -freflection")
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    string(APPEND CMAKE_REQUIRED_FLAGS
      " -freflection -fexpansion-statements")
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    string(APPEND CMAKE_REQUIRED_FLAGS " -freflection")
  endif()

  unset(TGE_COMPILER_HAS_CXX26_OPTIONS_REFLECTION CACHE)
  check_cxx_source_compiles([=[
    #include <glaze/glaze.hpp>
    #include <string>

    class Settings
    {
    public:
        Settings() = default;
        std::string name = "Tyrant";
        int workers = 4;
    };

    int main()
    {
        auto serialized = glz::write_json(Settings {});
        if (!serialized)
        {
            return 1;
        }

        Settings value;
        return glz::read_json(value, *serialized) ? 2 : 0;
    }
  ]=] TGE_COMPILER_HAS_CXX26_OPTIONS_REFLECTION)

  set(CMAKE_REQUIRED_DEFINITIONS "${_tge_saved_required_definitions}")
  set(CMAKE_REQUIRED_FLAGS "${_tge_saved_required_flags}")
  set(CMAKE_REQUIRED_INCLUDES "${_tge_saved_required_includes}")

  if(TGE_COMPILER_HAS_CXX26_OPTIONS_REFLECTION)
    set(TGE_OPTIONS_REFLECTION_ENABLED TRUE)
  endif()
endif()

if(TGE_OPTIONS_REFLECTION STREQUAL "ON" AND
   NOT TGE_OPTIONS_REFLECTION_ENABLED)
  message(FATAL_ERROR
    "TGE_OPTIONS_REFLECTION=ON requires a compiler and standard library with "
    "the C++26 reflection facilities used by Glaze.")
endif()

if(TGE_REFLECTION_DI_ENABLED)
  set(TGE_HAS_REFLECTION_DI_VALUE 1)
else()
  set(TGE_HAS_REFLECTION_DI_VALUE 0)
endif()

if(TGE_OPTIONS_REFLECTION_ENABLED)
  set(TGE_HAS_CXX26_OPTIONS_REFLECTION_VALUE 1)
  message(STATUS
    "TGE: enabling C++26 reflection-generated options serialization")
else()
  set(TGE_HAS_CXX26_OPTIONS_REFLECTION_VALUE 0)
  message(STATUS "TGE: using portable aggregate options serialization")
endif()

if(TGE_NATIVE_EXECUTION_ENABLED)
  set(TGE_HAS_NATIVE_STD_EXECUTION_VALUE 1)
  set(TGE_USES_STDEXEC_VALUE 0)
else()
  set(TGE_HAS_NATIVE_STD_EXECUTION_VALUE 0)
  set(TGE_USES_STDEXEC_VALUE 1)
endif()

set(TGE_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
configure_file(
  Runtime/Modules/Core/Public/TGE/Features.hpp.in
  "${TGE_GENERATED_INCLUDE_DIR}/TGE/Features.hpp"
  @ONLY)

# Centralize language requirements and capability selection so all public
# headers are parsed with the same configuration as the runtime.
add_library(tge_build_options INTERFACE)
add_library(TGE::build_options ALIAS tge_build_options)
target_compile_features(
  tge_build_options
  INTERFACE "cxx_std_${TGE_CMAKE_CXX_STANDARD}")
target_include_directories(
  tge_build_options
  INTERFACE
    $<BUILD_INTERFACE:${TGE_GENERATED_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(
  tge_build_options
  INTERFACE TGE::execution_backend TGE::glaze_headers)

target_compile_definitions(
  tge_build_options
  INTERFACE
    $<BUILD_INTERFACE:GLZ_REFLECTION26=${TGE_HAS_CXX26_OPTIONS_REFLECTION_VALUE}>)

if(TGE_REFLECTION_DI_ENABLED OR TGE_OPTIONS_REFLECTION_ENABLED)
  target_compile_options(
    tge_build_options
    INTERFACE
      $<BUILD_INTERFACE:$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:GNU,AppleClang>>:-freflection>>
      $<BUILD_INTERFACE:$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:Clang>>:-freflection;-fexpansion-statements>>)
endif()

# Add Subdirectories
add_subdirectory("Runtime")
add_subdirectory("Editor")

if(TGE_BUILD_DOCS)
  add_subdirectory("Docs")
endif()

if(TGE_BUILD_TESTS)
  include(CTest)

  if(BUILD_TESTING)
    enable_testing()
    add_subdirectory("Tests")
  endif()
endif()

configure_package_config_file(
  CMake/Config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/TyrantGameEngineConfig.cmake"
  INSTALL_DESTINATION "${TGE_PACKAGE_INSTALL_DIR}")
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/TyrantGameEngineConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion)
install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/TyrantGameEngineConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/TyrantGameEngineConfigVersion.cmake"
  DESTINATION "${TGE_PACKAGE_INSTALL_DIR}")
install(
  FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/TyrantGameEngine")

include(CPack)
