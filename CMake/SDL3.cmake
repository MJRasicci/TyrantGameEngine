include_guard(GLOBAL)

include(FetchContent)

# Fetch the exact SDL release approved for Tyrant's private window-platform
# adapter. Including this file is inert; the dependency is acquired only when
# the caller explicitly invokes TGE_FETCH_SDL3().
#
# Link TGE_SDL3_TARGET only from the concrete runtime/backend target with
# PRIVATE visibility. SDL headers, compile definitions, and target names must
# not enter Tyrant's public usage requirements.
#
# Any distribution that bundles the resulting SDL shared library must also
# bundle TGE_SDL3_LICENSE_FILE with its third-party license notices. This helper
# deliberately performs no install or packaging actions itself.
function(TGE_FETCH_SDL3)
    FetchContent_Declare(
        tge_sdl3
        URL
            "https://github.com/libsdl-org/SDL/releases/download/release-3.4.12/SDL3-3.4.12.tar.gz"
        URL_HASH
            "SHA256=f07b958a9ac5020fb7a44cadb957f658b2149c3c8abb4f63145fac9303249db7"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

    if(TARGET SDL3::SDL3-shared)
        FetchContent_GetProperties(tge_sdl3)
        if(NOT tge_sdl3_POPULATED)
            message(FATAL_ERROR
                "TGE_FETCH_SDL3 requires ownership of SDL3::SDL3-shared, "
                "but that target was defined by another dependency.")
        endif()
    else()
        # Build only the runtime artifact Tyrant intends to deploy. Keep SDL's
        # unrelated subsystems, tests, examples, and install rules out of the
        # parent project. In particular, rendering-context and surface
        # integration remain outside this backend's architectural boundary.
        set(SDL_SHARED ON CACHE BOOL "Build SDL as a shared library" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "Do not build SDL as a static library" FORCE)
        set(SDL_AUDIO OFF CACHE BOOL "Tyrant SDL backend does not use audio" FORCE)
        set(SDL_GPU OFF CACHE BOOL "Tyrant SDL backend does not use SDL GPU" FORCE)
        set(SDL_RENDER OFF CACHE BOOL "Tyrant SDL backend does not use SDL render" FORCE)
        set(SDL_CAMERA OFF CACHE BOOL "Tyrant SDL backend does not use cameras" FORCE)
        set(SDL_HAPTIC OFF CACHE BOOL "Tyrant SDL backend does not use haptics" FORCE)
        set(SDL_POWER OFF CACHE BOOL "Tyrant SDL backend does not use power APIs" FORCE)
        set(SDL_SENSOR OFF CACHE BOOL "Tyrant SDL backend does not use sensors" FORCE)
        set(SDL_DIALOG OFF CACHE BOOL "Tyrant SDL backend does not use dialogs" FORCE)
        set(SDL_TRAY OFF CACHE BOOL "Tyrant SDL backend does not use tray APIs" FORCE)
        set(SDL_OPENGL OFF CACHE BOOL "Rendering contexts are not an SDL backend concern" FORCE)
        set(SDL_OPENGLES OFF CACHE BOOL "Rendering contexts are not an SDL backend concern" FORCE)
        set(SDL_VULKAN OFF CACHE BOOL "Rendering surfaces are not an SDL backend concern" FORCE)
        set(SDL_METAL OFF CACHE BOOL "Rendering surfaces are not an SDL backend concern" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "Do not build SDL's test library" FORCE)
        set(SDL_TESTS OFF CACHE BOOL "Do not build SDL's tests" FORCE)
        set(SDL_EXAMPLES OFF CACHE BOOL "Do not build SDL's examples" FORCE)
        set(SDL_INSTALL OFF CACHE BOOL "Disable SDL install rules" FORCE)
        set(SDL_INSTALL_DOCS OFF CACHE BOOL
            "Disable SDL documentation install rules" FORCE)
        set(SDL_INSTALL_TESTS OFF CACHE BOOL
            "Disable SDL test install rules" FORCE)
        set(SDL_RPATH OFF CACHE BOOL
            "Tyrant owns the deployed runtime search path" FORCE)

        # SDL treats enabled-but-missing X11 extension packages as fatal. Keep
        # the base X11 backend available while disabling only optional
        # extensions absent from the active build environment.
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            include(CheckIncludeFile)

            macro(_tge_sdl3_optional_x11 option header library)
                string(MAKE_C_IDENTIFIER
                    "${option}_${header}"
                    _tge_sdl3_check_name)
                string(TOUPPER
                    "${_tge_sdl3_check_name}"
                    _tge_sdl3_check_name)
                check_include_file(
                    "${header}"
                    "TGE_HAS_${_tge_sdl3_check_name}")

                set(_tge_sdl3_has_library TRUE)
                if(NOT "${library}" STREQUAL "")
                    find_library(
                        "TGE_${option}_LIBRARY"
                        NAMES "${library}")
                    if(NOT TGE_${option}_LIBRARY)
                        set(_tge_sdl3_has_library FALSE)
                    endif()
                endif()

                if(NOT TGE_HAS_${_tge_sdl3_check_name} OR
                   NOT _tge_sdl3_has_library)
                    set(${option} OFF CACHE BOOL
                        "Disabled because its optional X11 dependency is unavailable"
                        FORCE)
                endif()
            endmacro()

            _tge_sdl3_optional_x11(
                SDL_X11_XCURSOR "X11/Xcursor/Xcursor.h" Xcursor)
            _tge_sdl3_optional_x11(
                SDL_X11_XDBE "X11/extensions/Xdbe.h" "")
            _tge_sdl3_optional_x11(
                SDL_X11_XINPUT "X11/extensions/XInput2.h" Xi)
            _tge_sdl3_optional_x11(
                SDL_X11_XFIXES "X11/extensions/Xfixes.h" Xfixes)
            _tge_sdl3_optional_x11(
                SDL_X11_XRANDR "X11/extensions/Xrandr.h" Xrandr)
            _tge_sdl3_optional_x11(
                SDL_X11_XSCRNSAVER "X11/extensions/scrnsaver.h" Xss)
            _tge_sdl3_optional_x11(
                SDL_X11_XSHAPE "X11/extensions/shape.h" "")
            _tge_sdl3_optional_x11(
                SDL_X11_XSYNC "X11/extensions/sync.h" Xext)
            _tge_sdl3_optional_x11(
                SDL_X11_XTEST "X11/extensions/XTest.h" Xtst)

            unset(_tge_sdl3_optional_x11)
        endif()

        # Keep the dependency safe for every parent linkage configuration
        # without changing the caller's CMAKE_POSITION_INDEPENDENT_CODE value.
        set(_tge_sdl3_parent_pic_defined FALSE)
        if(DEFINED CMAKE_POSITION_INDEPENDENT_CODE)
            set(_tge_sdl3_parent_pic_defined TRUE)
            set(_tge_sdl3_parent_pic
                "${CMAKE_POSITION_INDEPENDENT_CODE}")
        endif()
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)

        FetchContent_MakeAvailable(tge_sdl3)

        if(_tge_sdl3_parent_pic_defined)
            set(CMAKE_POSITION_INDEPENDENT_CODE
                "${_tge_sdl3_parent_pic}")
        else()
            unset(CMAKE_POSITION_INDEPENDENT_CODE)
        endif()
    endif()

    if(NOT TARGET SDL3::SDL3-shared)
        message(FATAL_ERROR
            "Pinned SDL 3.4.12 did not provide SDL3::SDL3-shared.")
    endif()

    FetchContent_GetProperties(tge_sdl3)
    set(TGE_SDL3_TARGET "SDL3::SDL3-shared" PARENT_SCOPE)
    set(TGE_SDL3_SOURCE_DIR "${tge_sdl3_SOURCE_DIR}" PARENT_SCOPE)
    set(TGE_SDL3_LICENSE_FILE
        "${tge_sdl3_SOURCE_DIR}/LICENSE.txt"
        PARENT_SCOPE)
endfunction()
