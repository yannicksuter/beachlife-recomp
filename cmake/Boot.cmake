# Boot build: the one merged game tree, linked with a loud trap for every
# function that is not decompiled yet. Trap hits choose the next work; a
# trap-backed build is never release gameplay. Linux x86-64 for now.
if(NOT BEACHLIFE_ENABLE_BOOT_GAME)
    return()
endif()
if(NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8))
    message(FATAL_ERROR "The boot build currently supports Linux x86-64 only")
endif()

set(BEACHLIFE_BOOT_INVENTORY "" CACHE FILEPATH
    "Optional progress inventory JSON that gives each trap its original address")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/game_tree.py" sources
        --tree "${BEACHLIFE_DECOMP_PREPARED}/beachlife-decomp"
        --compat "${CMAKE_SOURCE_DIR}/src/platform/compat/include"
    OUTPUT_VARIABLE beachlife_boot_sources COMMAND_ERROR_IS_FATAL ANY)
string(REPLACE "\n" ";" beachlife_boot_sources "${beachlife_boot_sources}")
list(FILTER beachlife_boot_sources EXCLUDE REGEX "^$")

# The Windows, DirectX and Miles API subset the retail game imports, on SDL3.
# Only the decompiled game sees its headers and MSVC prelude.
set(beachlife_compat_include "${CMAKE_SOURCE_DIR}/src/platform/compat/include")
add_library(beachlife_compat STATIC src/platform/compat/support.cpp src/platform/compat/kernel32.cpp
    src/platform/compat/kernel_strings.cpp
    src/platform/compat/kernel_files.cpp src/platform/compat/kernel_find.cpp
    src/platform/compat/kernel_ini.cpp src/platform/compat/kernel_mapping.cpp
    src/platform/compat/kernel_message.cpp src/platform/compat/kernel_system.cpp
    src/platform/compat/kernel_stdio.cpp
    src/platform/compat/kernel_wide_format.cpp
    src/platform/compat/user32.cpp src/platform/compat/gdi32.cpp src/platform/compat/gdi_dib.cpp
    src/platform/compat/user32_cursor.cpp src/platform/compat/cursor_resource.cpp
    src/platform/compat/cursor_provider.cpp
    src/platform/compat/ole32.cpp src/platform/compat/dinput.cpp src/platform/compat/input_map.cpp
    src/platform/compat/shell_link.cpp src/platform/compat/shell_link_com.cpp
    src/platform/compat/ddraw.cpp src/platform/compat/movie_guids.cpp
    src/platform/compat/movie_stream.cpp
    src/platform/compat/miles_audio.cpp src/platform/compat/miles.cpp)
target_include_directories(beachlife_compat PRIVATE "${beachlife_compat_include}"
    "${CMAKE_SOURCE_DIR}/src" "${BEACHLIFE_PREPARED}/dr-libs" "${BEACHLIFE_PREPARED}/pl-mpeg")
target_link_libraries(beachlife_compat PRIVATE SDL3::SDL3 freetype beachlife_retail_files)
target_compile_features(beachlife_compat PRIVATE cxx_std_20)
set_target_properties(beachlife_compat PROPERTIES CXX_EXTENSIONS OFF)
if(BUILD_TESTING)
    add_executable(compat_window_tests tests/compat_window.cpp)
    target_include_directories(compat_window_tests PRIVATE "${beachlife_compat_include}"
        "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(compat_window_tests PRIVATE beachlife_compat SDL3::SDL3)
    target_compile_features(compat_window_tests PRIVATE cxx_std_20)
    add_test(NAME compat_window COMMAND compat_window_tests)
    set_tests_properties(compat_window PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;BEACHLIFE_BOOT_QUIET=1" TIMEOUT 15)
    add_executable(compat_cursor_tests tests/compat_cursor.cpp src/runtime/retail_cursor.cpp)
    target_include_directories(compat_cursor_tests PRIVATE "${beachlife_compat_include}"
        "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(compat_cursor_tests PRIVATE beachlife_compat SDL3::SDL3)
    target_compile_features(compat_cursor_tests PRIVATE cxx_std_20)
    add_test(NAME compat_cursor COMMAND compat_cursor_tests)
    # The default parser test is headless. Run --sdl explicitly on a desktop to
    # also exercise VFS reads, the retail provider and real SDL cursor lifetime.
    set_tests_properties(compat_cursor PROPERTIES ENVIRONMENT "BEACHLIFE_BOOT_QUIET=1" TIMEOUT 15)
    add_executable(compat_movie_tests tests/compat_movie.cpp)
    target_include_directories(compat_movie_tests PRIVATE "${beachlife_compat_include}"
        "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(compat_movie_tests PRIVATE beachlife_compat beachlife_retail_files SDL3::SDL3)
    target_compile_features(compat_movie_tests PRIVATE cxx_std_20)
    add_test(NAME compat_movie COMMAND compat_movie_tests)
    set_tests_properties(compat_movie PROPERTIES ENVIRONMENT "BEACHLIFE_BOOT_QUIET=1" TIMEOUT 15)
    add_executable(compat_shell_link_tests tests/compat_shell_link.cpp)
    target_include_directories(compat_shell_link_tests PRIVATE "${beachlife_compat_include}"
        "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(compat_shell_link_tests PRIVATE beachlife_compat beachlife_retail_files SDL3::SDL3)
    target_compile_features(compat_shell_link_tests PRIVATE cxx_std_20)
    add_test(NAME compat_shell_link COMMAND compat_shell_link_tests)
    set_tests_properties(compat_shell_link PROPERTIES ENVIRONMENT "BEACHLIFE_BOOT_QUIET=1" TIMEOUT 15)
    add_executable(compat_input_tests tests/compat_input.cpp)
    target_include_directories(compat_input_tests PRIVATE "${beachlife_compat_include}"
        "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(compat_input_tests PRIVATE beachlife_compat SDL3::SDL3)
    target_compile_features(compat_input_tests PRIVATE cxx_std_20)
    set_target_properties(compat_input_tests PROPERTIES CXX_EXTENSIONS OFF)
    add_test(NAME compat_input COMMAND compat_input_tests)
    set_tests_properties(compat_input PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;BEACHLIFE_BOOT_QUIET=1" TIMEOUT 15)
    add_executable(compat_user_gdi_tests tests/compat_user_gdi.cpp)
    target_include_directories(compat_user_gdi_tests PRIVATE "${beachlife_compat_include}"
        "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(compat_user_gdi_tests PRIVATE beachlife_compat)
    target_compile_features(compat_user_gdi_tests PRIVATE cxx_std_20)
    add_test(NAME compat_user_gdi COMMAND compat_user_gdi_tests)
    set_tests_properties(compat_user_gdi PROPERTIES ENVIRONMENT "BEACHLIFE_BOOT_QUIET=1")
    add_executable(compat_files_tests tests/compat_files.cpp)
    target_include_directories(compat_files_tests PRIVATE "${beachlife_compat_include}"
        "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(compat_files_tests PRIVATE beachlife_compat beachlife_retail_files SDL3::SDL3)
    target_compile_features(compat_files_tests PRIVATE cxx_std_20)
    add_test(NAME compat_files COMMAND compat_files_tests)
    add_executable(compat_strings_tests tests/compat_strings.cpp)
    target_include_directories(compat_strings_tests PRIVATE "${beachlife_compat_include}")
    target_link_libraries(compat_strings_tests PRIVATE beachlife_compat)
    target_compile_features(compat_strings_tests PRIVATE cxx_std_20)
    add_test(NAME compat_strings COMMAND compat_strings_tests)
    add_executable(compat_wide_game_tests tests/compat_wide_game.cpp)
    target_include_directories(compat_wide_game_tests PRIVATE "${beachlife_compat_include}")
    target_link_libraries(compat_wide_game_tests PRIVATE beachlife_compat)
    target_compile_features(compat_wide_game_tests PRIVATE cxx_std_20)
    target_compile_options(compat_wide_game_tests PRIVATE -fshort-wchar
        -include "${beachlife_compat_include}/msvc_prelude.h")
    add_test(NAME compat_wide_game COMMAND compat_wide_game_tests)
endif()
# Media decoding and pixel conversion run every frame; unoptimized they cannot
# keep up with the movie and audio clocks in Debug builds.
if(NOT MSVC)
    set_source_files_properties(src/platform/compat/movie_stream.cpp src/platform/compat/ddraw.cpp
        src/platform/compat/miles_audio.cpp src/platform/compat/miles.cpp
        PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>")
endif()
add_library(beachlife_boot_game OBJECT ${beachlife_boot_sources})
target_link_libraries(beachlife_boot_game PRIVATE beachlife_runtime)
target_compile_options(beachlife_boot_game PRIVATE -w)

set(beachlife_boot_dir "${CMAKE_BINARY_DIR}/boot")
file(GENERATE OUTPUT "${beachlife_boot_dir}/objects.txt"
    CONTENT "$<JOIN:$<TARGET_OBJECTS:beachlife_boot_game>,\n>\n")
set(beachlife_boot_inventory)
if(BEACHLIFE_BOOT_INVENTORY)
    set(beachlife_boot_inventory --inventory "${BEACHLIFE_BOOT_INVENTORY}")
endif()
add_custom_command(
    OUTPUT "${beachlife_boot_dir}/traps.cpp" "${beachlife_boot_dir}/traps.json"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/game_tree.py" traps
        --objects "${beachlife_boot_dir}/objects.txt" ${beachlife_boot_inventory}
        --host $<TARGET_FILE:beachlife_compat> $<TARGET_FILE:beachlife_resource_pool_host>
        --tree "${BEACHLIFE_DECOMP_PREPARED}/beachlife-decomp"
        --output "${beachlife_boot_dir}/traps.cpp" --json "${beachlife_boot_dir}/traps.json"
    DEPENDS $<TARGET_OBJECTS:beachlife_boot_game> "${CMAKE_SOURCE_DIR}/tools/game_tree.py"
        beachlife_compat beachlife_resource_pool_host
    COMMENT "Generate boot traps for functions not decompiled yet"
    VERBATIM)

# The game tree plus its traps; Play links this when the boot build is enabled.
# Host services the tree calls: resource files, the retail cursor and photo export.
set(beachlife_boot_host beachlife_runtime beachlife_resource_files beachlife_jpeg_export
    beachlife_resource_pool_host beachlife_credits_data beachlife_compat)
add_library(beachlife_boot STATIC $<TARGET_OBJECTS:beachlife_boot_game>
    "${beachlife_boot_dir}/traps.cpp" src/runtime/boot_trap.cpp src/runtime/retail_cursor.cpp
    src/runtime/boot_session.cpp)
target_link_libraries(beachlife_boot PUBLIC ${beachlife_boot_host})
# Class traps include the tree headers of classes whose virtuals are missing.
beachlife_game_compile_environment(beachlife_boot)
target_compile_options(beachlife_boot PRIVATE -w)

# Links every tree object (not only referenced archive members), proving that
# the whole tree resolves with its traps.
add_executable(beachlife_boot_link src/runtime/boot_link.cpp
    $<TARGET_OBJECTS:beachlife_boot_game> "${beachlife_boot_dir}/traps.cpp"
    src/runtime/boot_trap.cpp src/runtime/retail_cursor.cpp)
target_link_libraries(beachlife_boot_link PRIVATE ${beachlife_boot_host})
beachlife_game_compile_environment(beachlife_boot_link)
target_compile_options(beachlife_boot_link PRIVATE -w)

# Play runs the decompiled entry instead of its placeholder frame. Whole
# archive: retail static initializers live in files nothing references, and
# the C runtime must still run them before WinMain.
target_link_libraries(beachlife PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,beachlife_boot>")
target_compile_definitions(beachlife PRIVATE BEACHLIFE_BOOT_GAME=1)
