cmake_minimum_required(VERSION 3.25)
project(TGEInstalledConsumer LANGUAGES CXX)

find_package(TyrantGameEngine CONFIG REQUIRED)

add_executable(tge_installed_consumer main.cpp)
target_link_libraries(tge_installed_consumer PRIVATE TGE::runtime)

enable_testing()
add_test(NAME InstalledConsumer.Runs COMMAND tge_installed_consumer)
if(TARGET TGE::runtime_shared)
    set_tests_properties(
        InstalledConsumer.Runs
        PROPERTIES
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:TGE::runtime_shared>")
endif()
