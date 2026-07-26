cmake_minimum_required(VERSION 3.25)
project(TyrantGameEngine VERSION 2.0.0.0 LANGUAGES CXX)
include(./CMake/Tyrant.cmake)

# Define version numbers
set(TGE_VERSION_MAJOR ${CMAKE_PROJECT_VERSION_MAJOR})
set(TGE_VERSION_MINOR ${CMAKE_PROJECT_VERSION_MINOR})
set(TGE_VERSION_PATCH ${CMAKE_PROJECT_VERSION_PATCH})

add_definitions(-DTGE_VERSION_MAJOR=${TGE_VERSION_MAJOR})
add_definitions(-DTGE_VERSION_MINOR=${TGE_VERSION_MINOR})
add_definitions(-DTGE_VERSION_PATCH=${TGE_VERSION_PATCH})

# Configure CPack
set(CPACK_PACKAGE_VERSION_MAJOR ${CMAKE_PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${CMAKE_PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${CMAKE_PROJECT_VERSION_PATCH})
include(CPack)

# Set the portable baseline. Reflection support is probed below and promotes
# participating targets to C++26 only when the required implementation exists.
set(CMAKE_CXX_STANDARD 23)
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
  TGE_REFLECTION_DI
  STRING
  AUTO
  "C++26 reflection-generated service activation: AUTO, ON, or OFF"
  AUTO ON OFF)

string(TOUPPER "${TGE_REFLECTION_DI}" TGE_REFLECTION_DI)
set(TGE_REFLECTION_DI "${TGE_REFLECTION_DI}" CACHE STRING
    "C++26 reflection-generated service activation: AUTO, ON, or OFF" FORCE)
set_property(CACHE TGE_REFLECTION_DI PROPERTY STRINGS AUTO ON OFF)

if(NOT TGE_REFLECTION_DI MATCHES "^(AUTO|ON|OFF)$")
  message(FATAL_ERROR
    "TGE_REFLECTION_DI must be AUTO, ON, or OFF (received '${TGE_REFLECTION_DI}').")
endif()

set(_tge_reflection_di_supported FALSE)

if(NOT TGE_REFLECTION_DI STREQUAL "OFF" AND
   "cxx_std_26" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
  include(CheckCXXSourceCompiles)

  set(_tge_saved_cxx_standard "${CMAKE_CXX_STANDARD}")
  set(_tge_saved_cxx_standard_required "${CMAKE_CXX_STANDARD_REQUIRED}")
  set(_tge_saved_cxx_extensions "${CMAKE_CXX_EXTENSIONS}")
  set(_tge_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")

  set(CMAKE_CXX_STANDARD 26)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  string(APPEND CMAKE_REQUIRED_FLAGS " -freflection")

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

  set(CMAKE_CXX_STANDARD "${_tge_saved_cxx_standard}")
  set(CMAKE_CXX_STANDARD_REQUIRED "${_tge_saved_cxx_standard_required}")
  set(CMAKE_CXX_EXTENSIONS "${_tge_saved_cxx_extensions}")
  set(CMAKE_REQUIRED_FLAGS "${_tge_saved_required_flags}")

  if(TGE_COMPILER_HAS_CXX26_REFLECTION_DI)
    set(_tge_reflection_di_supported TRUE)
  endif()
endif()

if(TGE_REFLECTION_DI STREQUAL "ON" AND NOT _tge_reflection_di_supported)
  message(FATAL_ERROR
    "TGE_REFLECTION_DI=ON requires a compiler and standard library with "
    "C++26 reflection support compatible with -freflection.")
endif()

if(_tge_reflection_di_supported AND NOT TGE_REFLECTION_DI STREQUAL "OFF")
  set(TGE_REFLECTION_DI_ENABLED TRUE)
  set(TGE_CXX_STANDARD 26)
  message(STATUS
    "TGE: enabling C++26 reflection-generated dependency injection")
else()
  set(TGE_REFLECTION_DI_ENABLED FALSE)
  set(TGE_CXX_STANDARD 23)
  message(STATUS
    "TGE: using portable traits-based dependency injection")
endif()

# Centralize language requirements so public headers are parsed with the same
# feature gate and compiler options as the runtime that produced them.
add_library(tge_build_options INTERFACE)
add_library(TGE::build_options ALIAS tge_build_options)
target_compile_features(tge_build_options INTERFACE "cxx_std_${TGE_CXX_STANDARD}")

if(TGE_REFLECTION_DI_ENABLED)
  target_compile_options(
    tge_build_options
    INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-freflection>)
  target_compile_definitions(
    tge_build_options
    INTERFACE TGE_HAS_REFLECTION_DI=1)
else()
  target_compile_definitions(
    tge_build_options
    INTERFACE TGE_HAS_REFLECTION_DI=0)
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
