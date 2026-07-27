cmake_minimum_required(VERSION 3.25)
project(TGEInstalledConsumer LANGUAGES CXX)

find_package(TyrantGameEngine CONFIG REQUIRED)

add_executable(tge_installed_consumer main.cpp)
target_link_libraries(tge_installed_consumer PRIVATE TGE::runtime)

enable_testing()
add_test(NAME InstalledConsumer.Runs COMMAND tge_installed_consumer)

set(_tge_runtime_path_modifications)
if(TARGET TGE::runtime_shared)
    list(APPEND
        _tge_runtime_path_modifications
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:TGE::runtime_shared>")
endif()
if(TARGET TGE::SDL3)
    # A static Tyrant package still links SDL's shared deployment artifact.
    # In particular, Windows consumers run outside the install bin directory
    # and need that imported runtime location on their DLL search path.
    list(APPEND
        _tge_runtime_path_modifications
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:TGE::SDL3>")
endif()
if(_tge_runtime_path_modifications)
    set_tests_properties(
        InstalledConsumer.Runs
        PROPERTIES
            ENVIRONMENT_MODIFICATION
                "${_tge_runtime_path_modifications}")
endif()
