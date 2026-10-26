# Real scheduler ownership and pool operations, independent of optional tests.
# All pool platform declarations resolve to the real host implementation.
add_library(beachlife_resource_pool_host STATIC src/runtime/resource_pool_host.cpp)
target_compile_features(beachlife_resource_pool_host PUBLIC cxx_std_20)
target_link_libraries(beachlife_resource_pool_host PUBLIC Threads::Threads)
set_target_properties(beachlife_resource_pool_host PROPERTIES
    CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
if(MSVC)
    target_compile_options(beachlife_resource_pool_host PRIVATE /GL-)
else()
    target_compile_options(beachlife_resource_pool_host PRIVATE -fno-lto)
endif()

add_library(beachlife_reconstructed_runtime_pool STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/pool_allocate.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/pool_chunk_acquire.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/pool_return.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/pool_lock_acquire.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/pool_lock_guard.cpp")
target_link_libraries(beachlife_reconstructed_runtime_pool PUBLIC
    beachlife_resource_pool_host)

add_library(beachlife_reconstructed_game_update_pair_owner STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/startup_game_update_pair_constructor.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_game_update_pair_destructor.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_game_point_sound_list_base.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_game_point_sound_list_destructor.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_game_object_sound_list_destructor.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_game_update_pair_registration_destructor.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_game_update_pair_globals.cpp")
target_link_libraries(beachlife_reconstructed_game_update_pair_owner PUBLIC
    beachlife_reconstructed_runtime_pool)

foreach(target IN ITEMS beachlife_reconstructed_runtime_pool
    beachlife_reconstructed_game_update_pair_owner)
    target_include_directories(${target} PUBLIC "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(${target} PUBLIC cxx_std_20)
    add_dependencies(${target} verify_reconstructed_source)
    set_target_properties(${target} PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(${target} PRIVATE /GL-)
    else()
        target_compile_options(${target} PRIVATE -fno-lto)
    endif()
endforeach()
