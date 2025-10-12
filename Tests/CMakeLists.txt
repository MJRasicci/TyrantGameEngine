cmake_minimum_required(VERSION 3.25)

include(CTest)

function(TGE_TEST_MODULE module)
    if("${module}" STREQUAL "")
        message(FATAL_ERROR "TGE_TEST_MODULE: missing module name")
    endif()

    set(module_dir "${CMAKE_CURRENT_SOURCE_DIR}/${module}")
    if(NOT IS_DIRECTORY "${module_dir}")
        message(FATAL_ERROR "TGE_TEST_MODULE ${module}: directory not found at ${module_dir}")
    endif()

    file(GLOB_RECURSE test_sources CONFIGURE_DEPENDS
        "${module_dir}/*.c"
        "${module_dir}/*.cc"
        "${module_dir}/*.cxx"
        "${module_dir}/*.cpp")

    if(test_sources STREQUAL "")
        message(WARNING "TGE_TEST_MODULE ${module}: no test sources found under ${module_dir}")
        return()
    endif()

    foreach(src IN LISTS test_sources)
        file(RELATIVE_PATH rel_path "${module_dir}" "${src}")
        get_filename_component(stem "${rel_path}" NAME_WE)
        get_filename_component(rel_dir "${rel_path}" DIRECTORY)

        if(rel_dir STREQUAL "" OR rel_dir STREQUAL ".")
            set(test_name "${module}.${stem}")
            set(target_suffix "${stem}")
        else()
            string(REPLACE "/" "." rel_dir_dots "${rel_dir}")
            set(test_name "${module}.${rel_dir_dots}.${stem}")
            set(target_suffix "${rel_dir}/${stem}")
        endif()

        string(REPLACE "/" "_" target_suffix "${target_suffix}")
        string(REGEX REPLACE "[^A-Za-z0-9_]" "_" target_suffix "${target_suffix}")
        set(target "tge_test_${module}_${target_suffix}")

        add_executable(${target} ${src})
        target_compile_features(${target} PRIVATE cxx_std_23)
        target_link_libraries(${target} PRIVATE TGE::runtime)
        set_target_properties(${target} PROPERTIES FOLDER "Tests/${module}")

        add_test(NAME "${test_name}" COMMAND ${target})
    endforeach()
endfunction()

# Register runtime module test suites here
TGE_TEST_MODULE(Core)
