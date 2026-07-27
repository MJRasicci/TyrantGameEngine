cmake_minimum_required(VERSION 3.30)

# Options specific to runtime (more modules in future)
# (Core is typically required by everything; we mark it REQUIRED below.)
# Per-module options are auto-created by TGE_MODULE()

# Register modules
# Core module lives at Runtime/Modules/Core/{Public,Private}
TGE_MODULE(Core REQUIRED)
TGE_MODULE(Application REQUIRED DEPS Core)
TGE_MODULE(CliApplication REQUIRED DEPS Application)

# One externally driven desktop queue serializes private platform adapters.
# Its headers are implementation contracts and never enter the installed API.
TGE_MODULE(DesktopEventRuntime REQUIRED INTERNAL_ONLY DEPS Core)
add_library(tge_desktop_event_runtime_spi INTERFACE)
add_library(
    TGE::desktop_event_runtime_spi
    ALIAS tge_desktop_event_runtime_spi)
target_link_libraries(
    tge_desktop_event_runtime_spi
    INTERFACE TGE::DesktopEventRuntime_obj)
target_include_directories(
    tge_desktop_event_runtime_spi
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Modules/DesktopEventRuntime/Private/include>")

TGE_MODULE(Graphics REQUIRED DEPS Core DesktopEventRuntime)
target_link_libraries(
    tge_Graphics_obj
    PRIVATE TGE::desktop_event_runtime_spi)

# Platform adapters consume Graphics implementation contracts through this
# build-tree-only target. The SPI remains unavailable to installed consumers
# and does not become part of Graphics' public API.
add_library(tge_graphics_platform_spi INTERFACE)
add_library(TGE::graphics_platform_spi ALIAS tge_graphics_platform_spi)
target_link_libraries(
    tge_graphics_platform_spi
    INTERFACE
        TGE::Graphics_obj
        TGE::desktop_event_runtime_spi)
target_include_directories(
    tge_graphics_platform_spi
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Modules/Graphics/Private/include>")

set(TGE_HAS_VULKAN_PRESENTATION FALSE)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TGE_MODULE_SDLDESKTOP)
    TGE_MODULE(
        VulkanPresentation
        INTERNAL_ONLY
        DEPS Graphics)
    if(TGE_MODULE_VULKANPRESENTATION)
        set(TGE_HAS_VULKAN_PRESENTATION TRUE)
        find_package(Vulkan REQUIRED)
        target_link_libraries(
            tge_VulkanPresentation_obj
            PRIVATE
                TGE::graphics_platform_spi
                Vulkan::Vulkan)
        add_library(tge_vulkan_presentation_spi INTERFACE)
        add_library(
            TGE::vulkan_presentation_spi
            ALIAS tge_vulkan_presentation_spi)
        target_link_libraries(
            tge_vulkan_presentation_spi
            INTERFACE
                TGE::VulkanPresentation_obj
                TGE::graphics_platform_spi)
        target_include_directories(
            tge_vulkan_presentation_spi
            INTERFACE
                "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Modules/VulkanPresentation/Private/include>")
    endif()
endif()

TGE_MODULE(Input REQUIRED DEPS Core DesktopEventRuntime)
target_link_libraries(
    tge_Input_obj
    PRIVATE TGE::desktop_event_runtime_spi)

add_library(tge_input_platform_spi INTERFACE)
add_library(TGE::input_platform_spi ALIAS tge_input_platform_spi)
target_link_libraries(
    tge_input_platform_spi
    INTERFACE
        TGE::Input_obj
        TGE::desktop_event_runtime_spi)
target_include_directories(
    tge_input_platform_spi
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Modules/Input/Private/include>")

TGE_MODULE(
    SDLDesktop
    INTERNAL_ONLY
    DEPS DesktopEventRuntime Graphics Input)
if(TGE_MODULE_SDLDESKTOP)
    include("${CMAKE_SOURCE_DIR}/CMake/SDL3.cmake")
    TGE_FETCH_SDL3()

    target_link_libraries(
        tge_SDLDesktop_obj
        PRIVATE
            TGE::desktop_event_runtime_spi
            TGE::graphics_platform_spi
            TGE::input_platform_spi
            ${TGE_SDL3_TARGET})

    add_library(tge_sdl_desktop_spi INTERFACE)
    add_library(TGE::sdl_desktop_spi ALIAS tge_sdl_desktop_spi)
    target_link_libraries(
        tge_sdl_desktop_spi
        INTERFACE
            TGE::SDLDesktop_obj
            TGE::desktop_event_runtime_spi
            TGE::graphics_platform_spi
            TGE::input_platform_spi)
    target_include_directories(
        tge_sdl_desktop_spi
        INTERFACE
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Modules/SDLDesktop/Private/include>")
endif()

TGE_MODULE(
    GuiApplication
    REQUIRED
    DEPS Application Graphics Input DesktopEventRuntime)
target_link_libraries(
    tge_GuiApplication_obj
    PRIVATE
        TGE::desktop_event_runtime_spi
        TGE::graphics_platform_spi
        TGE::input_platform_spi)
if(TGE_MODULE_SDLDESKTOP)
    target_link_libraries(
        tge_GuiApplication_obj
        PRIVATE TGE::sdl_desktop_spi)
    target_compile_definitions(
        tge_GuiApplication_obj
        PRIVATE TGE_HAS_SDL_DESKTOP_BACKEND=1)
else()
    target_compile_definitions(
        tge_GuiApplication_obj
        PRIVATE TGE_HAS_SDL_DESKTOP_BACKEND=0)
endif()
if(TGE_HAS_VULKAN_PRESENTATION)
    target_link_libraries(
        tge_GuiApplication_obj
        PRIVATE TGE::vulkan_presentation_spi)
    target_compile_definitions(
        tge_GuiApplication_obj
        PRIVATE TGE_HAS_VULKAN_PRESENTATION=1)
else()
    target_compile_definitions(
        tge_GuiApplication_obj
        PRIVATE TGE_HAS_VULKAN_PRESENTATION=0)
endif()

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
if(TGE_MODULE_SDLDESKTOP)
    if(TGE_BUILD_SHARED)
        target_link_libraries(
            ${_tge_runtime_target}
            PRIVATE
                "$<BUILD_INTERFACE:$<TARGET_LINKER_FILE:${TGE_SDL3_TARGET}>>")
        add_dependencies(
            ${_tge_runtime_target}
            SDL3-shared)
    else()
        target_link_libraries(
            ${_tge_runtime_target}
            PUBLIC
                "$<BUILD_INTERFACE:${TGE_SDL3_TARGET}>"
                "$<INSTALL_INTERFACE:TGE::SDL3>")
    endif()

    if(APPLE)
        set_property(
            TARGET ${_tge_runtime_target}
            PROPERTY INSTALL_RPATH "@loader_path")
    elseif(UNIX)
        set_property(
            TARGET ${_tge_runtime_target}
            PROPERTY INSTALL_RPATH "$ORIGIN")
    endif()
endif()
if(TGE_HAS_VULKAN_PRESENTATION)
    if(TGE_BUILD_SHARED)
        target_link_libraries(
            ${_tge_runtime_target}
            PRIVATE Vulkan::Vulkan)
        set(TGE_REQUIRES_VULKAN FALSE PARENT_SCOPE)
    else()
        target_link_libraries(
            ${_tge_runtime_target}
            PUBLIC
                "$<BUILD_INTERFACE:Vulkan::Vulkan>"
                "$<INSTALL_INTERFACE:Vulkan::Vulkan>")
        set(TGE_REQUIRES_VULKAN TRUE PARENT_SCOPE)
    endif()
else()
    set(TGE_REQUIRES_VULKAN FALSE PARENT_SCOPE)
endif()
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

if(TGE_MODULE_SDLDESKTOP)
    # SDL is a private implementation dependency, but its shared runtime and
    # license must accompany both static and shared Tyrant distributions.
    install(
        TARGETS SDL3-shared
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(
        FILES "${TGE_SDL3_LICENSE_FILE}"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/TyrantGameEngine"
        RENAME SDL3-LICENSE.txt)
    set(TGE_INSTALLS_SDL3 TRUE PARENT_SCOPE)
else()
    set(TGE_INSTALLS_SDL3 FALSE PARENT_SCOPE)
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
