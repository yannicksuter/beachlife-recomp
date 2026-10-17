# Build only the libraries from verified prepared sources. No system zlib or
# OpenSSL lookup, dependency programs, or installation rules are needed.
function(beachlife_add_cabinet_dependencies)
    set(ZLIB_BUILD_SHARED OFF)
    set(ZLIB_BUILD_STATIC ON)
    set(ZLIB_BUILD_TESTING OFF)
    set(ZLIB_INSTALL OFF)
    add_subdirectory("${BEACHLIFE_PREPARED}/zlib" "${CMAKE_BINARY_DIR}/extern/zlib" EXCLUDE_FROM_ALL)
    add_dependencies(zlibstatic verify_prepared)

    set(unshield "${BEACHLIFE_PREPARED}/unshield")
    set(config "${CMAKE_BINARY_DIR}/extern/unshield")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/unshield_config.h.in"
        "${config}/lib/unshield_config.h" @ONLY)
    add_library(beachlife_unshield STATIC
        "${unshield}/lib/component.c" "${unshield}/lib/converter.c"
        "${unshield}/lib/directory.c" "${unshield}/lib/file.c"
        "${unshield}/lib/file_group.c" "${unshield}/lib/helper.c"
        "${unshield}/lib/libunshield.c" "${unshield}/lib/log.c"
        "${unshield}/lib/md5/md5c.c")
    set_target_properties(beachlife_unshield PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
    target_include_directories(beachlife_unshield SYSTEM PUBLIC "${unshield}/lib")
    target_include_directories(beachlife_unshield PRIVATE "${config}")
    if(MSVC)
        target_include_directories(beachlife_unshield PRIVATE "${unshield}/win32_msvc")
        target_compile_definitions(beachlife_unshield PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    target_link_libraries(beachlife_unshield PRIVATE ZLIB::ZLIBSTATIC)
    add_dependencies(beachlife_unshield verify_prepared)
endfunction()
beachlife_add_cabinet_dependencies()
