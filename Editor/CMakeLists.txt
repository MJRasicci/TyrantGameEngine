cmake_minimum_required(VERSION 3.25)

project(TGE_Editor LANGUAGES CXX)

# Collect editor sources
file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cxx")
if(EDITOR_SOURCES STREQUAL "")
    message(FATAL_ERROR "Editor: no sources found under ${CMAKE_CURRENT_SOURCE_DIR}/source")
endif()

add_executable(tge_editor ${EDITOR_SOURCES})

target_compile_features(tge_editor PUBLIC "cxx_std_${TGE_CXX_STANDARD}")
target_include_directories(tge_editor
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# Link the runtime (points to static or shared via alias)
target_link_libraries(tge_editor PRIVATE TGE::runtime)

set_target_properties(tge_editor PROPERTIES
    OUTPUT_NAME "TyrantEditor"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

if(APPLE)
    set_property(
        TARGET tge_editor
        PROPERTY INSTALL_RPATH
            "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
elseif(UNIX)
    set_property(
        TARGET tge_editor
        PROPERTY INSTALL_RPATH
            "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()

install(
    TARGETS tge_editor
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/Editor"
    FILES_MATCHING
        PATTERN "*.hpp"
        PATTERN "*.h")
