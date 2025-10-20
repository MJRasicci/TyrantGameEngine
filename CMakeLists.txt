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

# Set C++ standard
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
