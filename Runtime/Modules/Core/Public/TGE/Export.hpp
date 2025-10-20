/**
 * @file Export.hpp
 * @brief Cross-platform macro definitions for the Tyrant Game Engine runtime.
 *
 * This file contains the macro definitions necessary for exporting and importing
 * the classes and functions of the Tyrant Game Engine runtime, facilitating the
 * creation and use of a DLL or shared object across different platforms and compilers.
 */

#pragma once

//===========================================================================//
//=======> Define TGE_API <================================================//
//===========================================================================//

#if defined(TGE_SHARED) // Check if shared build is defined

    #if defined(_WIN32) || defined(_WIN64) // Windows OS

        #ifdef BUILDING_TGE
            /**
             * @def TGE_API
             * @brief Macro for exporting classes and functions from the Tyrant DLL.
             *
             * When BUILDING_TGE is defined (during the building of the Tyrant library),
             * this macro ensures symbols are exported to the DLL.
             */
            #define TGE_API __declspec(dllexport)
        #else
            /**
             * @def TGE_API
             * @brief Macro for importing classes and functions into the Tyrant application.
             *
             * When BUILDING_TGE is not defined (when using the Tyrant library),
             * this macro ensures symbols are imported from the DLL.
             */
            #define TGE_API __declspec(dllimport)
        #endif

        // Disable warnings for exported STL objects, if using MSVC
        #ifdef _MSC_VER
            #pragma warning(disable : 4251)
        #endif

    #else // Unix-like OS (Linux, macOS)

        // Using modern GCC or Clang versions
        #if defined(__GNUC__) || defined(__clang__)
            #if __has_attribute(visibility)
                /**
                 * @def TGE_API
                 * @brief Macro for exporting/importing classes and functions from/to the Tyrant shared object.
                 *
                 * When using GCC or Clang compilers, this macro ensures symbols are appropriately
                 * exported or imported using the 'visibility' attribute.
                 */
                #define TGE_API __attribute__ ((__visibility__("default")))
            #else
                #define TGE_API
            #endif
        #else
            #error "Unsupported compiler. Please define TGE_API for your compiler."
        #endif

    #endif

#else

    /**
     * @def TGE_API
     * @brief Macro for internal linkage in static builds of the TGE runtime.
     *
     * When TGE_SHARED is defined, indicating a shared build of the library,
     * this macro leaves symbols with internal linkage, as import/export is not required.
     */
    #define TGE_API

#endif
