cmake_minimum_required(VERSION 3.31)

project(TGE_Editor LANGUAGES CXX)

# Collect editor sources
file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cxx")
if(EDITOR_SOURCES STREQUAL "")
    message(FATAL_ERROR "Editor: no sources found under ${CMAKE_CURRENT_SOURCE_DIR}/source")
endif()

add_executable(tge_editor ${EDITOR_SOURCES})

target_compile_features(tge_editor PUBLIC cxx_std_23)
target_include_directories(tge_editor
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

# Link the runtime (points to static or shared via alias)
target_link_libraries(tge_editor PRIVATE TGE::runtime)

set_target_properties(tge_editor PROPERTIES
    OUTPUT_NAME "TyrantEditor"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

install(TARGETS tge_editor RUNTIME DESTINATION bin)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION include/Editor FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")
