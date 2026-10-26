# Every native consumer of prepared game source uses the same headers and ABI.
# Keep these settings private so they do not propagate to host consumers or
# fixture executables. Each target still chooses its own providers at link time.
function(beachlife_game_compile_environment target)
    target_include_directories(${target} PRIVATE
        "${BEACHLIFE_DECOMP_SOURCE}" "${CMAKE_SOURCE_DIR}/src")
    target_compile_features(${target} PRIVATE cxx_std_20)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    add_dependencies(${target} verify_reconstructed_source)

    if(BEACHLIFE_ENABLE_BOOT_GAME)
        set(compat_include "${CMAKE_SOURCE_DIR}/src/platform/compat/include")
        target_include_directories(${target} PRIVATE "${compat_include}")
        target_compile_definitions(${target} PRIVATE BEACHLIFE_COMPAT=1)
        target_compile_options(${target} PRIVATE
            -fshort-wchar -include "${compat_include}/msvc_prelude.h")
        if(APPLE AND BEACHLIFE_ARCH STREQUAL "arm64")
            # Preserve the recovered separate multiply/add operations on ARM.
            # This does not emulate x87's retained extended precision.
            target_compile_options(${target} PRIVATE -ffp-contract=off)
        endif()
    endif()
endfunction()
