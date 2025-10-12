cmake_minimum_required(VERSION 3.25)

# Ensure doxygen
find_package(Doxygen QUIET)
if(NOT Doxygen_FOUND)
    message(STATUS "Doxygen not found; skipping documentation targets.")
    return()
endif()

# --------------------------------------------
# Bring in doxygen-awesome-css via FetchContent
include(FetchContent)
FetchContent_Declare(
    doxygen-awesome-css
    URL https://github.com/jothepro/doxygen-awesome-css/archive/refs/heads/main.zip
)
FetchContent_MakeAvailable(doxygen-awesome-css)

# Path to unpacked resources (doxygen-awesome.css, addons, etc.)
FetchContent_GetProperties(doxygen-awesome-css SOURCE_DIR AWESOME_CSS_DIR)

# --------------------------------------------
# Doxygen config I/O
set(DOXYFILE_IN   "${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in")
set(DOXYFILE_OUT  "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")

# Variables exposed to Doxyfile.in via @VAR@ substitution
set(DOXYGEN_AWESOME_CSS_DIR     "${AWESOME_CSS_DIR}")
set(DOXYGEN_PROJECT_SOURCE_DIR  "${PROJECT_SOURCE_DIR}")
set(DOXYGEN_PROJECT_BINARY_DIR  "${PROJECT_BINARY_DIR}")
set(DOXYGEN_GENERATED_OUTPUT_DIR "${DOXYGEN_OUTPUT_DIR}")

# --------------------------------------------
# Reconfigure when pages, styles, or Doxyfile template change
file(GLOB_RECURSE DOC_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Pages/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/Styles/*"
    "${DOXYFILE_IN}"
)

# Generate an actual Doxyfile with substituted variables
configure_file("${DOXYFILE_IN}" "${DOXYFILE_OUT}" @ONLY)

# --------------------------------------------
# Build docs: model as a custom command that produces a concrete output
# (the index.html) and a custom target depending on that file.
# This gives proper up-to-date checks and lets install() consume outputs.

# The primary artifact we expect after running Doxygen:
set(DOXYGEN_HTML_INDEX "${DOXYGEN_OUTPUT_DIR}/html/index.html")

add_custom_command(
    OUTPUT  "${DOXYGEN_HTML_INDEX}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DOXYGEN_OUTPUT_DIR}"
    COMMAND "${DOXYGEN_EXECUTABLE}" "${DOXYFILE_OUT}"
    # Doxygen scans source tree; working dir doesn’t strictly matter,
    # but using the project binary dir keeps relative paths sensible.
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
    DEPENDS ${DOC_SOURCES}
    COMMENT "Generating API documentation with Doxygen → ${DOXYGEN_OUTPUT_DIR}"
    VERBATIM
)

add_custom_target(TyrantGameEngineDocs ALL
    DEPENDS "${DOXYGEN_HTML_INDEX}"
)

# --------------------------------------------
# Install the generated site so CPack will include it.
install(
    DIRECTORY "${DOXYGEN_OUTPUT_DIR}/html/"
    DESTINATION "docs"
    COMPONENT docs
)

# Expose an explicit 'docs' install component label in GUIs/installers.
# Safe to leave even for TGZ/ZIP packaging.
set_property(INSTALL COMPONENT docs PROPERTY DISPLAY_NAME "Documentation")
