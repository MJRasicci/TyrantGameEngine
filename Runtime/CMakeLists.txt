cmake_minimum_required(VERSION 3.25)

# Options specific to runtime (more modules in future)
# (Core is typically required by everything; we mark it REQUIRED below.)
# Per-module options are auto-created by TGE_MODULE()

# Register modules
# Core module lives at Runtime/Modules/Core/{Public,Private}
TGE_MODULE(Core REQUIRED)

# In future:
# TGE_MODULE(Assets   DEPS Core)
# TGE_MODULE(Audio    DEPS Core Assets)
# TGE_MODULE(Network  DEPS Core)

# Enforce REQUIRED rules now that all modules are declared
TGE_VALIDATE_REQUIRED_MODULES()

# --- Aggregate libraries from selected module objects --------------------------
# Static aggregate
add_library(tge_runtime_static STATIC ${TGE_SELECTED_OBJECTS})
add_library(TGE::runtime_static ALIAS tge_runtime_static)
set_target_properties(tge_runtime_static PROPERTIES OUTPUT_NAME "tge_runtime")
target_link_libraries(tge_runtime_static PUBLIC ${TGE_SELECTED_MODULES})
set_property(TARGET tge_runtime_static PROPERTY POSITION_INDEPENDENT_CODE ON)

# Optional shared aggregate
if(TGE_BUILD_SHARED)
    add_library(tge_runtime_shared SHARED ${TGE_SELECTED_OBJECTS})
    add_library(TGE::runtime_shared ALIAS tge_runtime_shared)
    set_target_properties(tge_runtime_shared PROPERTIES OUTPUT_NAME "tge_runtime")
    target_link_libraries(tge_runtime_shared PUBLIC ${TGE_SELECTED_MODULES})

    # Define for public headers to switch import/export
    target_compile_definitions(tge_runtime_shared PUBLIC TGE_SHARED)

    target_include_directories(tge_runtime_shared PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>)
endif()

# Public convenience alias that points to the chosen linkage
add_library(tge_runtime INTERFACE)
add_library(TGE::runtime ALIAS tge_runtime)

if(TGE_BUILD_SHARED)
  target_link_libraries(tge_runtime INTERFACE TGE::runtime_shared)
else()
  target_link_libraries(tge_runtime INTERFACE TGE::runtime_static)
endif()

# ------------------ Auto-install public headers per module ---------------------
get_property(_tge_pub_inc_dirs GLOBAL PROPERTY TGE_PUBLIC_INCLUDE_DIRS)
if(_tge_pub_inc_dirs)
    # Deduplicate
    list(REMOVE_DUPLICATES _tge_pub_inc_dirs)
    foreach(_dir IN LISTS _tge_pub_inc_dirs)
        set(_tge_subdir "${_dir}/TGE")
        if(EXISTS "${_tge_subdir}")
            # Install into include/TGE while preserving the module's public hierarchy
            install(DIRECTORY "${_tge_subdir}/" DESTINATION include/TGE FILES_MATCHING
                    PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.hh" PATTERN "*.hxx" PATTERN "*.inl")
        endif()
    endforeach()
endif()

# Libraries
install(TARGETS tge_runtime_static
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin)

if(TGE_BUILD_SHARED)
    install(TARGETS tge_runtime_shared
            ARCHIVE DESTINATION lib
            LIBRARY DESTINATION lib
            RUNTIME DESTINATION bin)
endif()
