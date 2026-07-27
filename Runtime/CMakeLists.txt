cmake_minimum_required(VERSION 3.25)

# Options specific to runtime (more modules in future)
# (Core is typically required by everything; we mark it REQUIRED below.)
# Per-module options are auto-created by TGE_MODULE()

# Register modules
# Core module lives at Runtime/Modules/Core/{Public,Private}
TGE_MODULE(Core REQUIRED)
TGE_MODULE(Graphics REQUIRED DEPS Core)

# In future:
# TGE_MODULE(Assets   DEPS Core)
# TGE_MODULE(Audio    DEPS Core Assets)
# TGE_MODULE(Network  DEPS Core)

# Enforce REQUIRED rules now that all modules are declared
TGE_VALIDATE_REQUIRED_MODULES()

# Public include roots are repeated on the aggregate because module object
# libraries are deliberately build-only and never leak into installed exports.
get_property(_tge_pub_inc_dirs GLOBAL PROPERTY TGE_PUBLIC_INCLUDE_DIRS)
if(_tge_pub_inc_dirs)
    list(REMOVE_DUPLICATES _tge_pub_inc_dirs)
endif()

# Build exactly the linkage selected by TGE_BUILD_SHARED. This avoids duplicate
# Windows import/static .lib names and ensures object export definitions match
# the archive in which they are used.
if(TGE_BUILD_SHARED)
    add_library(tge_runtime_shared SHARED ${TGE_SELECTED_OBJECTS})
    add_library(TGE::runtime_shared ALIAS tge_runtime_shared)
    set(_tge_runtime_target tge_runtime_shared)
    set_target_properties(
        tge_runtime_shared
        PROPERTIES
            OUTPUT_NAME "tge_runtime"
            EXPORT_NAME runtime_shared)
    target_compile_definitions(tge_runtime_shared PUBLIC TGE_SHARED)
else()
    add_library(tge_runtime_static STATIC ${TGE_SELECTED_OBJECTS})
    add_library(TGE::runtime_static ALIAS tge_runtime_static)
    set(_tge_runtime_target tge_runtime_static)
    set_target_properties(
        tge_runtime_static
        PROPERTIES
            OUTPUT_NAME "tge_runtime"
            EXPORT_NAME runtime_static
            POSITION_INDEPENDENT_CODE ON)
endif()

target_link_libraries(${_tge_runtime_target} PUBLIC TGE::build_options)
target_include_directories(
    ${_tge_runtime_target}
    PUBLIC
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
foreach(_dir IN LISTS _tge_pub_inc_dirs)
    target_include_directories(
        ${_tge_runtime_target}
        PUBLIC $<BUILD_INTERFACE:${_dir}>)
endforeach()

# Public convenience alias that points to the chosen linkage
add_library(tge_runtime INTERFACE)
add_library(TGE::runtime ALIAS tge_runtime)
set_target_properties(tge_runtime PROPERTIES EXPORT_NAME runtime)
target_link_libraries(
    tge_runtime
    INTERFACE TGE::build_options ${_tge_runtime_target})

# ------------------ Auto-install public headers per module ---------------------
if(_tge_pub_inc_dirs)
    foreach(_dir IN LISTS _tge_pub_inc_dirs)
        set(_tge_subdir "${_dir}/TGE")
        if(EXISTS "${_tge_subdir}")
            # Install into include/TGE while preserving the module's public hierarchy
            install(DIRECTORY "${_tge_subdir}/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/TGE" FILES_MATCHING
                    PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.hh" PATTERN "*.hxx" PATTERN "*.inl")
        endif()
    endforeach()
endif()

# Install the generated feature configuration selected for this runtime.
install(
    FILES "${TGE_GENERATED_INCLUDE_DIR}/TGE/Features.hpp"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/TGE")

# Public asynchronous APIs use stdexec when the active standard library does
# not yet provide the C++26 execution facilities. Package the pinned headers so
# installed consumers use the same backend as the runtime.
if(TGE_STDEXEC_SOURCE_DIR)
    install(
        DIRECTORY
            "${TGE_STDEXEC_SOURCE_DIR}/include/stdexec"
            "${TGE_STDEXEC_SOURCE_DIR}/include/exec"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(
        FILES "${TGE_STDEXEC_SOURCE_DIR}/LICENSE.txt"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/TyrantGameEngine"
        RENAME stdexec-LICENSE.txt)
endif()

# Options serialization is part of TGE's public template surface, so installed
# consumers receive the exact pinned Glaze headers used to build the runtime.
install(
    DIRECTORY "${TGE_GLAZE_SOURCE_DIR}/include/glaze"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(
    FILES "${TGE_GLAZE_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/TyrantGameEngine"
    RENAME glaze-LICENSE.txt)

set_target_properties(
    tge_build_options
    PROPERTIES EXPORT_NAME build_options)
set_target_properties(
    tge_execution_backend
    PROPERTIES EXPORT_NAME execution_backend)
set_target_properties(
    tge_glaze_headers
    PROPERTIES EXPORT_NAME glaze_headers)

install(
    TARGETS
        tge_runtime
        ${_tge_runtime_target}
        tge_build_options
        tge_execution_backend
        tge_glaze_headers
    EXPORT TyrantGameEngineTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(
    EXPORT TyrantGameEngineTargets
    FILE TyrantGameEngineTargets.cmake
    NAMESPACE TGE::
    DESTINATION "${TGE_PACKAGE_INSTALL_DIR}")
