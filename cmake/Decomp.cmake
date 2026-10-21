option(BEACHLIFE_BUILD_RECONSTRUCTED_TESTS "Build tests for reconstructed subsystems" OFF)
if(BEACHLIFE_BUILD_RECONSTRUCTED_TESTS AND NOT BUILD_TESTING)
    message(FATAL_ERROR "BEACHLIFE_BUILD_RECONSTRUCTED_TESTS requires BUILD_TESTING")
endif()

# The selected commit must exist locally; configuration never fetches source.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/patches/beachlife-decomp/source.json")
file(READ "${CMAKE_SOURCE_DIR}/patches/beachlife-decomp/source.json" BEACHLIFE_DECOMP_PIN)
string(JSON BEACHLIFE_DECOMP_PIN_SIZE LENGTH "${BEACHLIFE_DECOMP_PIN}")
if(NOT BEACHLIFE_DECOMP_PIN_SIZE EQUAL 1)
    message(FATAL_ERROR "The reconstructed source pin must contain only a revision")
endif()
string(JSON BEACHLIFE_DECOMP_REVISION GET "${BEACHLIFE_DECOMP_PIN}" revision)
set(BEACHLIFE_DECOMP_PREPARED "${CMAKE_BINARY_DIR}/prepared")
set(BEACHLIFE_DECOMP_SOURCE "${BEACHLIFE_DECOMP_PREPARED}/beachlife-decomp/src")
set(BEACHLIFE_DECOMP_PREPARE_COMMAND
    "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/prepare_sources.py"
    --output "${BEACHLIFE_DECOMP_PREPARED}"
    --decomp-revision "${BEACHLIFE_DECOMP_REVISION}"
    --decomp-profile "${BEACHLIFE_DECOMP_PROFILE}" --decomp-only)
execute_process(COMMAND ${BEACHLIFE_DECOMP_PREPARE_COMMAND} COMMAND_ERROR_IS_FATAL ANY)
add_custom_target(verify_reconstructed_source
    COMMAND ${BEACHLIFE_DECOMP_PREPARE_COMMAND} --verify
    COMMENT "Verify the pinned reconstructed source and faithful patches"
    VERBATIM)

# Native compilation uses the host CRT, without the VC6 comparison support.
add_library(beachlife_reconstructed_files STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/file_size.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/file_loader.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/ble_loader.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_read.cpp")
target_include_directories(beachlife_reconstructed_files PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(beachlife_reconstructed_files PUBLIC cxx_std_20)
target_link_libraries(beachlife_reconstructed_files PUBLIC beachlife_retail_files)
set_target_properties(beachlife_reconstructed_files PROPERTIES CXX_EXTENSIONS OFF)
add_dependencies(beachlife_reconstructed_files verify_reconstructed_source)

add_library(beachlife_reconstructed_config STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_separator.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_name_delimiters.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_name_extent.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_value_extent.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_quote_seeker.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_token_count.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_string_extent.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_value_classifier.cpp")
target_include_directories(beachlife_reconstructed_config PUBLIC "${BEACHLIFE_DECOMP_SOURCE}")
target_compile_features(beachlife_reconstructed_config PUBLIC cxx_std_20)
set_target_properties(beachlife_reconstructed_config PROPERTIES CXX_EXTENSIONS OFF)
add_dependencies(beachlife_reconstructed_config verify_reconstructed_source)

add_library(beachlife_reconstructed_config_tokens STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_cleanup.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_token_reset.cpp")
target_include_directories(beachlife_reconstructed_config_tokens PUBLIC "${BEACHLIFE_DECOMP_SOURCE}")
target_compile_features(beachlife_reconstructed_config_tokens PUBLIC cxx_std_20)
set_target_properties(beachlife_reconstructed_config_tokens PROPERTIES CXX_EXTENSIONS OFF)
add_dependencies(beachlife_reconstructed_config_tokens verify_reconstructed_source)

add_library(beachlife_reconstructed_config_allocation STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_token_allocation.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_token_reset.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_cleanup.cpp")
target_include_directories(beachlife_reconstructed_config_allocation PUBLIC "${BEACHLIFE_DECOMP_SOURCE}")
target_compile_features(beachlife_reconstructed_config_allocation PUBLIC cxx_std_20)
set_target_properties(beachlife_reconstructed_config_allocation PROPERTIES CXX_EXTENSIONS OFF)
add_dependencies(beachlife_reconstructed_config_allocation verify_reconstructed_source)

add_library(beachlife_reconstructed_config_parser STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_token_name.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_decimal.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_token_value.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_token_parser.cpp")
target_include_directories(beachlife_reconstructed_config_parser PUBLIC "${BEACHLIFE_DECOMP_SOURCE}")
target_compile_features(beachlife_reconstructed_config_parser PUBLIC cxx_std_20)
target_link_libraries(beachlife_reconstructed_config_parser PUBLIC
    beachlife_reconstructed_config beachlife_reconstructed_config_tokens)
set_target_properties(beachlife_reconstructed_config_parser PROPERTIES CXX_EXTENSIONS OFF)
add_dependencies(beachlife_reconstructed_config_parser verify_reconstructed_source)

add_library(beachlife_reconstructed_config_loading STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_buffer_constructor.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_load_parse.cpp")
target_include_directories(beachlife_reconstructed_config_loading PUBLIC "${BEACHLIFE_DECOMP_SOURCE}")
target_compile_features(beachlife_reconstructed_config_loading PUBLIC cxx_std_20)
target_link_libraries(beachlife_reconstructed_config_loading PUBLIC
    beachlife_reconstructed_files beachlife_reconstructed_config_parser
    beachlife_reconstructed_config_allocation)
set_target_properties(beachlife_reconstructed_config_loading PROPERTIES CXX_EXTENSIONS OFF)
add_dependencies(beachlife_reconstructed_config_loading verify_reconstructed_source)

add_library(beachlife_reconstructed_startup_config STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_owner_constructor.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_owner_destructor.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_append.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_owner_reset.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_fatal.cpp")
target_include_directories(beachlife_reconstructed_startup_config PUBLIC "${BEACHLIFE_DECOMP_SOURCE}")
target_compile_features(beachlife_reconstructed_startup_config PUBLIC cxx_std_20)
target_link_libraries(beachlife_reconstructed_startup_config PUBLIC beachlife_reconstructed_config_loading)
set_target_properties(beachlife_reconstructed_startup_config PROPERTIES CXX_EXTENSIONS OFF)
add_dependencies(beachlife_reconstructed_startup_config verify_reconstructed_source)

# Normal Play constructs this host owner from its completed VFS mount. Its
# implementation consumes prepared source; no retail input is required to build.
add_library(beachlife_startup_configuration STATIC
    src/runtime/startup_configuration.cpp src/runtime/config_snapshot.cpp)
target_include_directories(beachlife_startup_configuration PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(beachlife_startup_configuration PUBLIC cxx_std_20)
target_link_libraries(beachlife_startup_configuration PRIVATE
    beachlife_reconstructed_startup_config beachlife_formats)
set_target_properties(beachlife_startup_configuration PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(beachlife_startup_configuration PRIVATE /W4 /WX)
else()
    target_compile_options(beachlife_startup_configuration PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(BUILD_TESTING)
    add_executable(startup_configuration_tests tests/startup_configuration.cpp)
    add_executable(startup_configuration_default_tests tests/startup_configuration.cpp)
    target_compile_definitions(startup_configuration_default_tests PRIVATE
        STARTUP_CONFIGURATION_DEFAULT_ALLOCATOR)
    foreach(target startup_configuration_tests startup_configuration_default_tests)
        target_link_libraries(${target} PRIVATE beachlife_startup_configuration
            beachlife_reconstructed_startup_config beachlife_formats)
        set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /WX)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
    endforeach()
    add_test(NAME startup_configuration
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/check_startup_configuration.py"
            "$<TARGET_FILE:startup_configuration_tests>" "$<TARGET_FILE:startup_configuration_default_tests>")
    set_tests_properties(startup_configuration PROPERTIES LABELS "reconstructed;headless")
endif()

add_library(beachlife_reconstructed_surface_pixels STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/startup_surface_pixel_access.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/surface_error_sink.cpp")
target_include_directories(beachlife_reconstructed_surface_pixels PUBLIC
    "${BEACHLIFE_DECOMP_SOURCE}")
target_compile_features(beachlife_reconstructed_surface_pixels PUBLIC cxx_std_20)
set_target_properties(beachlife_reconstructed_surface_pixels PROPERTIES CXX_EXTENSIONS OFF)
add_dependencies(beachlife_reconstructed_surface_pixels verify_reconstructed_source)

if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    target_compile_definitions(beachlife_reconstructed_surface_pixels PUBLIC
        BEACHLIFE_DECOMP_RENDERER_QUEUE=1)
endif()

# Reconstructed runtime libraries remain available without subsystem tests.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_render_masks STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/render_mask_setup.cpp")
    target_include_directories(beachlife_reconstructed_render_masks PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_render_masks PUBLIC cxx_std_20)
    set_target_properties(beachlife_reconstructed_render_masks PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    add_dependencies(beachlife_reconstructed_render_masks verify_reconstructed_source)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_render_masks PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_render_masks PRIVATE -fno-lto)
    endif()

    add_library(beachlife_reconstructed_renderer_present_effect STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_present_effect.cpp")
    target_include_directories(beachlife_reconstructed_renderer_present_effect PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_renderer_present_effect PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_renderer_present_effect PUBLIC
        beachlife_reconstructed_render_masks)
    set_target_properties(beachlife_reconstructed_renderer_present_effect PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    add_dependencies(beachlife_reconstructed_renderer_present_effect verify_reconstructed_source)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_renderer_present_effect PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_renderer_present_effect PRIVATE -fno-lto)
    endif()

    add_library(beachlife_reconstructed_renderer_default STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_render_default.cpp")
    target_include_directories(beachlife_reconstructed_renderer_default PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_renderer_default PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_renderer_default PUBLIC
        beachlife_reconstructed_surface_pixels beachlife_reconstructed_render_masks)
    set_target_properties(beachlife_reconstructed_renderer_default PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    add_dependencies(beachlife_reconstructed_renderer_default verify_reconstructed_source)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_renderer_default PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_renderer_default PRIVATE -fno-lto)
    endif()

    add_library(beachlife_reconstructed_resource_raster_402880 STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_resource_raster_402880.cpp")
    target_include_directories(beachlife_reconstructed_resource_raster_402880 PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_resource_raster_402880 PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_resource_raster_402880 PUBLIC
        beachlife_reconstructed_render_masks)
    set_target_properties(beachlife_reconstructed_resource_raster_402880 PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    add_dependencies(beachlife_reconstructed_resource_raster_402880 verify_reconstructed_source)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_resource_raster_402880 PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_resource_raster_402880 PRIVATE -fno-lto)
    endif()

    add_library(beachlife_reconstructed_resource_raster_405b20 STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_resource_raster_405b20.cpp")
    target_include_directories(beachlife_reconstructed_resource_raster_405b20 PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_resource_raster_405b20 PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_resource_raster_405b20 PUBLIC
        beachlife_reconstructed_render_masks)
    set_target_properties(beachlife_reconstructed_resource_raster_405b20 PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    add_dependencies(beachlife_reconstructed_resource_raster_405b20 verify_reconstructed_source)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_resource_raster_405b20 PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_resource_raster_405b20 PRIVATE -fno-lto)
    endif()

    add_library(beachlife_reconstructed_resource_raster_direct STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_resource_raster1_direct.cpp")
    add_library(beachlife_reconstructed_resource_raster2 STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_resource_raster2.cpp")
    target_link_libraries(beachlife_reconstructed_resource_raster2 PUBLIC
        beachlife_reconstructed_resource_raster_direct)
    foreach(target beachlife_reconstructed_resource_raster_direct
            beachlife_reconstructed_resource_raster2)
        target_include_directories(${target} PUBLIC "${BEACHLIFE_DECOMP_SOURCE}")
        target_compile_features(${target} PUBLIC cxx_std_20)
        set_target_properties(${target} PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        add_dependencies(${target} verify_reconstructed_source)
        if(MSVC)
            target_compile_options(${target} PRIVATE /GL-)
        else()
            target_compile_options(${target} PRIVATE -fno-lto)
        endif()
    endforeach()
endif()

if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_resource_files STATIC src/runtime/resource_files.cpp)
    target_include_directories(beachlife_resource_files PUBLIC "${CMAKE_SOURCE_DIR}/src")
    target_compile_features(beachlife_resource_files PUBLIC cxx_std_20)
    target_link_libraries(beachlife_resource_files PUBLIC beachlife_data Threads::Threads)
    set_target_properties(beachlife_resource_files PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_resource_files PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_resource_files PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()

# Real mode selection and progress-file loading, available without subsystem tests.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_game_mode_progress STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_game_set_byte_a4.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_game_scenario_progress.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_game_progress_path.cpp")
    target_include_directories(beachlife_reconstructed_game_mode_progress PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_game_mode_progress PUBLIC cxx_std_20)
    add_dependencies(beachlife_reconstructed_game_mode_progress verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_game_mode_progress PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_game_mode_progress PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_game_mode_progress PRIVATE -fno-lto)
    endif()
endif()

# Standalone reconstructed sea-state lifetime and byte setter.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_sea_state_owner STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_sea_state_constructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_sea_state_destructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_sea_state_enabled.cpp")
    target_include_directories(beachlife_reconstructed_sea_state_owner PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_sea_state_owner PUBLIC cxx_std_20)
    add_dependencies(beachlife_reconstructed_sea_state_owner verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_sea_state_owner PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_sea_state_owner PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_sea_state_owner PRIVATE -fno-lto)
    endif()
endif()

# Reconstructed renderer forwarding to the genuine sea-state owner.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_renderer_sea_enabled STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_sea_enabled.cpp")
    target_include_directories(beachlife_reconstructed_renderer_sea_enabled PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_renderer_sea_enabled PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_renderer_sea_enabled PUBLIC
        beachlife_reconstructed_sea_state_owner)
    add_dependencies(beachlife_reconstructed_renderer_sea_enabled verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_renderer_sea_enabled PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_renderer_sea_enabled PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_renderer_sea_enabled PRIVATE -fno-lto)
    endif()
endif()

# Genuine sound-bank FILE, record and cached-sample ownership.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_sound_bank STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/sound_capacity.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/window_sound_bank_begin.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/window_sound_bank_constructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/window_sound_bank_destructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/window_sound_bank_load_index.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/window_sound_bucket_value.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/window_sound_end.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/window_sound_find_key.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/window_sound_find_sample.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/window_sound_index_release.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/window_sound_release_sample.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/window_sound_reset_buckets.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/window_sound_storage_helpers.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/window_sound_update_cache.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/sound_name_hash.cpp")
    target_include_directories(beachlife_reconstructed_sound_bank PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_sound_bank PUBLIC cxx_std_20)
    add_dependencies(beachlife_reconstructed_sound_bank verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_sound_bank PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_sound_bank PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_sound_bank PRIVATE -fno-lto)
    endif()
endif()

if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_listbox_selection_find STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_list_box_find.cpp")
    target_include_directories(beachlife_reconstructed_listbox_selection_find PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_listbox_selection_find PUBLIC cxx_std_20)
    add_dependencies(beachlife_reconstructed_listbox_selection_find verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_listbox_selection_find PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_listbox_selection_find PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_listbox_selection_find PRIVATE -fno-lto)
    endif()
endif()

# Genuine idle Mode6 owner and its real base/derived lifetimes.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_screen_mode6 STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_mode6.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_mode6_destructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_mode6_idle.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_mode_controller.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_mode_controller_destructor.cpp")
    target_include_directories(beachlife_reconstructed_screen_mode6 PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_screen_mode6 PUBLIC cxx_std_20)
    add_dependencies(beachlife_reconstructed_screen_mode6 verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_screen_mode6 PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_screen_mode6 PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_screen_mode6 PRIVATE -fno-lto)
    endif()
endif()

# Genuine frame-clock owner and the existing sole input/clock global TU.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_frame_clock STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_frame_clock_constructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_frame_clock_destructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_frame_clock.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_input_globals.cpp")
    target_include_directories(beachlife_reconstructed_frame_clock PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_frame_clock PUBLIC cxx_std_20)
    add_dependencies(beachlife_reconstructed_frame_clock verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_frame_clock PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_frame_clock PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_frame_clock PRIVATE -fno-lto)
    endif()
endif()

# The floating-point reader has only its real host CRT dependency. Keep this
# library available independently of optional reconstructed subsystem fixtures.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_gui_float STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_float.cpp")
    target_include_directories(beachlife_reconstructed_gui_float PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_gui_float PUBLIC cxx_std_20)
    set_target_properties(beachlife_reconstructed_gui_float PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    add_dependencies(beachlife_reconstructed_gui_float verify_reconstructed_source)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_gui_float PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_gui_float PRIVATE -fno-lto)
    endif()
endif()


# Insert before the reconstructed-test early return. Both libraries are usable
# without the optional generated test and remain experimental-only.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_jpeg_export STATIC src/runtime/jpeg_export.cpp)
    target_include_directories(beachlife_jpeg_export PUBLIC "${CMAKE_SOURCE_DIR}/src")
    target_include_directories(beachlife_jpeg_export SYSTEM PRIVATE
        "${BEACHLIFE_PREPARED}/stb")
    target_compile_features(beachlife_jpeg_export PUBLIC cxx_std_20)
    add_dependencies(beachlife_jpeg_export verify_prepared)

    add_library(beachlife_reconstructed_jpeg_export STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_jpeg_export.cpp")
    target_include_directories(beachlife_reconstructed_jpeg_export PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_jpeg_export PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_jpeg_export PUBLIC beachlife_jpeg_export)
    add_dependencies(beachlife_reconstructed_jpeg_export verify_reconstructed_source)
    set_target_properties(beachlife_jpeg_export beachlife_reconstructed_jpeg_export PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_jpeg_export PRIVATE /GL- /W4 /WX)
        target_compile_options(beachlife_reconstructed_jpeg_export PRIVATE /GL-)
    else()
        target_compile_options(beachlife_jpeg_export PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(beachlife_reconstructed_jpeg_export PRIVATE -fno-lto)
    endif()
endif()


if(NOT BEACHLIFE_BUILD_RECONSTRUCTED_TESTS)
    return()
endif()

add_library(beachlife_reconstructed_tga STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/tga_image_constructor.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/tga_image_cleanup.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/tga_palette.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/tga_pixel_data.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/tga_palette_copy.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/tga_pixel_copy.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/tga_rle.cpp")
target_include_directories(beachlife_reconstructed_tga PUBLIC "${BEACHLIFE_DECOMP_SOURCE}")
target_compile_features(beachlife_reconstructed_tga PUBLIC cxx_std_20)
set_target_properties(beachlife_reconstructed_tga PROPERTIES CXX_EXTENSIONS OFF)
add_dependencies(beachlife_reconstructed_tga verify_reconstructed_source)

add_executable(reconstructed_tga_tests tests/reconstructed_tga.cpp)
target_link_libraries(reconstructed_tga_tests PRIVATE beachlife_reconstructed_tga)
set_target_properties(reconstructed_tga_tests PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(reconstructed_tga_tests PRIVATE /W4 /WX)
else()
    target_compile_options(reconstructed_tga_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME reconstructed_tga COMMAND reconstructed_tga_tests)
set_tests_properties(reconstructed_tga PROPERTIES LABELS "reconstructed;headless")

add_executable(reconstructed_files_tests tests/reconstructed_files.cpp)
target_link_libraries(reconstructed_files_tests PRIVATE beachlife_reconstructed_files)
set_target_properties(reconstructed_files_tests PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(reconstructed_files_tests PRIVATE /W4 /WX)
else()
    target_compile_options(reconstructed_files_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
set(BEACHLIFE_RECONSTRUCTED_FIXTURE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/reconstructed-files-fixture"
    CACHE STRING "Writable target-system directory for generated reconstructed file tests")
add_test(NAME reconstructed_files COMMAND reconstructed_files_tests
    "${BEACHLIFE_RECONSTRUCTED_FIXTURE_ROOT}")
set_tests_properties(reconstructed_files PROPERTIES LABELS "reconstructed;headless")

add_executable(reconstructed_config_tests tests/reconstructed_config.cpp)
target_link_libraries(reconstructed_config_tests PRIVATE beachlife_reconstructed_config)
set_target_properties(reconstructed_config_tests PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(reconstructed_config_tests PRIVATE /W4 /WX)
else()
    target_compile_options(reconstructed_config_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME reconstructed_config COMMAND reconstructed_config_tests)
set_tests_properties(reconstructed_config PROPERTIES LABELS "reconstructed;headless")

add_executable(reconstructed_config_tokens_tests tests/reconstructed_config_tokens.cpp)
target_link_libraries(reconstructed_config_tokens_tests PRIVATE beachlife_reconstructed_config_loading)
set_target_properties(reconstructed_config_tokens_tests PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(reconstructed_config_tokens_tests PRIVATE /W4 /WX)
else()
    target_compile_options(reconstructed_config_tokens_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME reconstructed_config_tokens COMMAND reconstructed_config_tokens_tests)
set_tests_properties(reconstructed_config_tokens PROPERTIES LABELS "reconstructed;headless")

add_executable(reconstructed_config_allocation_tests tests/reconstructed_config_allocation.cpp)
target_link_libraries(reconstructed_config_allocation_tests PRIVATE beachlife_reconstructed_config_loading)
set_target_properties(reconstructed_config_allocation_tests PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(reconstructed_config_allocation_tests PRIVATE /W4 /WX)
else()
    target_compile_options(reconstructed_config_allocation_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME reconstructed_config_allocation COMMAND reconstructed_config_allocation_tests)
set_tests_properties(reconstructed_config_allocation PROPERTIES LABELS "reconstructed;headless")

add_executable(reconstructed_config_parser_tests tests/reconstructed_config_parser.cpp)
target_link_libraries(reconstructed_config_parser_tests PRIVATE beachlife_reconstructed_config_loading)
set_target_properties(reconstructed_config_parser_tests PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(reconstructed_config_parser_tests PRIVATE /W4 /WX)
else()
    target_compile_options(reconstructed_config_parser_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME reconstructed_config_parser COMMAND reconstructed_config_parser_tests)
set_tests_properties(reconstructed_config_parser PROPERTIES LABELS "reconstructed;headless")

add_executable(reconstructed_config_loading_tests tests/reconstructed_config_loading.cpp)
target_link_libraries(reconstructed_config_loading_tests PRIVATE beachlife_reconstructed_config_loading)
set_target_properties(reconstructed_config_loading_tests PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(reconstructed_config_loading_tests PRIVATE /W4 /WX)
else()
    target_compile_options(reconstructed_config_loading_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
set(BEACHLIFE_RECONSTRUCTED_CONFIG_FIXTURE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/reconstructed-config-fixture"
    CACHE STRING "Writable target-system directory for generated configuration loading tests")
add_test(NAME reconstructed_config_loading COMMAND reconstructed_config_loading_tests
    "${BEACHLIFE_RECONSTRUCTED_CONFIG_FIXTURE_ROOT}")
set_tests_properties(reconstructed_config_loading PROPERTIES LABELS "reconstructed;headless")

add_executable(reconstructed_startup_config_tests tests/reconstructed_startup_config.cpp)
target_link_libraries(reconstructed_startup_config_tests PRIVATE beachlife_reconstructed_startup_config)
set_target_properties(reconstructed_startup_config_tests PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(reconstructed_startup_config_tests PRIVATE /W4 /WX)
else()
    target_compile_options(reconstructed_startup_config_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
set(BEACHLIFE_RECONSTRUCTED_STARTUP_FIXTURE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/reconstructed-startup-fixture"
    CACHE STRING "Writable target-system directory for generated startup configuration tests")
add_test(NAME reconstructed_startup_config COMMAND reconstructed_startup_config_tests
    "${BEACHLIFE_RECONSTRUCTED_STARTUP_FIXTURE_ROOT}")
set_tests_properties(reconstructed_startup_config PROPERTIES LABELS "reconstructed;headless")

add_library(beachlife_reconstructed_input STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/startup_input_state_predicate.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_input_state_one.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_input_transition_reset.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_input_getter_first.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_input_getter_second.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_input_first_button.cpp")
target_include_directories(beachlife_reconstructed_input PUBLIC "${BEACHLIFE_DECOMP_SOURCE}")
target_compile_features(beachlife_reconstructed_input PUBLIC cxx_std_20)
set_target_properties(beachlife_reconstructed_input PROPERTIES CXX_EXTENSIONS OFF)
add_dependencies(beachlife_reconstructed_input verify_reconstructed_source)

add_executable(reconstructed_input_tests tests/reconstructed_input.cpp)
target_link_libraries(reconstructed_input_tests PRIVATE beachlife_reconstructed_input)
set_target_properties(reconstructed_input_tests PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(reconstructed_input_tests PRIVATE /W4 /WX)
else()
    target_compile_options(reconstructed_input_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME reconstructed_input COMMAND reconstructed_input_tests)
set_tests_properties(reconstructed_input PROPERTIES LABELS "reconstructed;headless")

add_library(beachlife_reconstructed_cursor STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/startup_input_cursor_setter.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_input_current_cursor.cpp"
    src/runtime/retail_cursor.cpp)
target_include_directories(beachlife_reconstructed_cursor PUBLIC
    "${BEACHLIFE_DECOMP_SOURCE}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(beachlife_reconstructed_cursor PUBLIC cxx_std_20)
target_link_libraries(beachlife_reconstructed_cursor PUBLIC Threads::Threads)
set_target_properties(beachlife_reconstructed_cursor PROPERTIES CXX_EXTENSIONS OFF)
add_dependencies(beachlife_reconstructed_cursor verify_reconstructed_source)

add_executable(reconstructed_cursor_tests tests/reconstructed_cursor.cpp)
target_link_libraries(reconstructed_cursor_tests PRIVATE beachlife_reconstructed_cursor)
set_target_properties(reconstructed_cursor_tests PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(reconstructed_cursor_tests PRIVATE /W4 /WX)
else()
    target_compile_options(reconstructed_cursor_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME reconstructed_cursor COMMAND reconstructed_cursor_tests)
set_tests_properties(reconstructed_cursor PROPERTIES LABELS "reconstructed;headless")

add_library(beachlife_reconstructed_surfaces STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/startup_surface_position.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_surface_cleanup.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_surface_indexed_cleanup.cpp")
target_include_directories(beachlife_reconstructed_surfaces PUBLIC "${BEACHLIFE_DECOMP_SOURCE}")
target_compile_features(beachlife_reconstructed_surfaces PUBLIC cxx_std_20)
set_target_properties(beachlife_reconstructed_surfaces PROPERTIES CXX_EXTENSIONS OFF)
add_dependencies(beachlife_reconstructed_surfaces verify_reconstructed_source)

add_executable(reconstructed_surfaces_tests tests/reconstructed_surfaces.cpp)
target_link_libraries(reconstructed_surfaces_tests PRIVATE beachlife_reconstructed_surfaces)
set_target_properties(reconstructed_surfaces_tests PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(reconstructed_surfaces_tests PRIVATE /W4 /WX)
else()
    target_compile_options(reconstructed_surfaces_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME reconstructed_surfaces COMMAND reconstructed_surfaces_tests)
set_tests_properties(reconstructed_surfaces PROPERTIES LABELS "reconstructed;headless")

# This isolated test provider supplies deterministic platform calls to the
# unchanged reconstructed methods. It is not linked into the game runtime.
add_library(beachlife_reconstructed_pool_lock STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/pool_lock_acquire.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/pool_lock_guard.cpp")
target_include_directories(beachlife_reconstructed_pool_lock PUBLIC
    "${CMAKE_SOURCE_DIR}/tests/providers/pool_lock" "${BEACHLIFE_DECOMP_SOURCE}")
target_compile_features(beachlife_reconstructed_pool_lock PUBLIC cxx_std_20)
set_target_properties(beachlife_reconstructed_pool_lock PROPERTIES CXX_EXTENSIONS OFF)
add_dependencies(beachlife_reconstructed_pool_lock verify_reconstructed_source)

add_executable(reconstructed_pool_lock_tests tests/reconstructed_pool_lock.cpp)
target_link_libraries(reconstructed_pool_lock_tests PRIVATE beachlife_reconstructed_pool_lock)
set_target_properties(reconstructed_pool_lock_tests PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(reconstructed_pool_lock_tests PRIVATE /W4 /WX)
else()
    target_compile_options(reconstructed_pool_lock_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME reconstructed_pool_lock COMMAND reconstructed_pool_lock_tests)
set_tests_properties(reconstructed_pool_lock PROPERTIES
    LABELS "reconstructed;headless" TIMEOUT 30)

add_executable(reconstructed_surface_pixels_tests tests/reconstructed_surface_pixels.cpp)
target_link_libraries(reconstructed_surface_pixels_tests PRIVATE
    beachlife_reconstructed_surface_pixels)
set_target_properties(reconstructed_surface_pixels_tests PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(reconstructed_surface_pixels_tests PRIVATE /W4 /WX)
else()
    target_compile_options(reconstructed_surface_pixels_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME reconstructed_surface_pixels COMMAND reconstructed_surface_pixels_tests)
set_tests_properties(reconstructed_surface_pixels PROPERTIES
    LABELS "reconstructed;headless" TIMEOUT 30)

# The fixture supplies scoped scalar allocation and deterministic platform calls;
# every reconstructed method is compiled unchanged from the native export.
add_library(beachlife_reconstructed_screen_list STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/startup_list_node.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_list_insert.cpp"
    "${BEACHLIFE_DECOMP_PREPARED}/beachlife-decomp/support/startup_list_begin.cpp"
    "${BEACHLIFE_DECOMP_PREPARED}/beachlife-decomp/support/startup_list_end.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/pool_chunk_acquire.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/pool_return.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/pool_lock_acquire.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/pool_lock_guard.cpp")
target_include_directories(beachlife_reconstructed_screen_list PUBLIC
    "${CMAKE_SOURCE_DIR}/tests/providers/pool_lock" "${BEACHLIFE_DECOMP_SOURCE}")
target_compile_features(beachlife_reconstructed_screen_list PUBLIC cxx_std_20)
add_dependencies(beachlife_reconstructed_screen_list verify_reconstructed_source)

add_executable(reconstructed_screen_list_tests tests/reconstructed_screen_list.cpp)
target_link_libraries(reconstructed_screen_list_tests PRIVATE
    beachlife_reconstructed_screen_list)
set_target_properties(beachlife_reconstructed_screen_list reconstructed_screen_list_tests
    PROPERTIES CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
if(MSVC)
    target_compile_options(beachlife_reconstructed_screen_list PRIVATE /GL-)
    target_compile_options(reconstructed_screen_list_tests PRIVATE /GL- /W4 /WX)
else()
    target_compile_options(beachlife_reconstructed_screen_list PRIVATE -fno-lto)
    target_compile_options(reconstructed_screen_list_tests PRIVATE
        -fno-lto -Wall -Wextra -Wpedantic -Werror)
    target_link_options(reconstructed_screen_list_tests PRIVATE -fno-lto)
endif()
add_test(NAME reconstructed_screen_list COMMAND reconstructed_screen_list_tests)
set_tests_properties(reconstructed_screen_list PROPERTIES
    LABELS "reconstructed;headless" TIMEOUT 30)

# Generated objects exercise the real lifetime and callbacks. Allocation/release
# providers belong only to this test executable; no VC6 support body is linked.
add_library(beachlife_reconstructed_screen_item STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_position.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_prepare.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_constructor.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_destructor.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_rectangle.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_prepare.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_position.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_move.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_name_search.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_placement.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_list_adaptor.cpp"
    "${BEACHLIFE_DECOMP_PREPARED}/beachlife-decomp/support/startup_list_begin.cpp"
    "${BEACHLIFE_DECOMP_PREPARED}/beachlife-decomp/support/startup_list_end.cpp")
target_include_directories(beachlife_reconstructed_screen_item PUBLIC
    "${BEACHLIFE_DECOMP_SOURCE}")
# Every consumer of this optional test archive sees the same test-only
# inherited-constructor declaration. Production targets do not define it.
target_compile_definitions(beachlife_reconstructed_screen_item PUBLIC
    BEACHLIFE_DECOMP_TEST_OBJECTS=1)
target_compile_features(beachlife_reconstructed_screen_item PUBLIC cxx_std_20)
add_dependencies(beachlife_reconstructed_screen_item verify_reconstructed_source)

add_executable(reconstructed_screen_item_tests tests/reconstructed_screen_item.cpp)
target_link_libraries(reconstructed_screen_item_tests PRIVATE
    beachlife_reconstructed_screen_item)
set_target_properties(beachlife_reconstructed_screen_item reconstructed_screen_item_tests
    PROPERTIES CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
if(MSVC)
    target_compile_options(beachlife_reconstructed_screen_item PRIVATE /GL-)
    target_compile_options(reconstructed_screen_item_tests PRIVATE /GL- /W4 /WX)
else()
    target_compile_options(beachlife_reconstructed_screen_item PRIVATE -fno-lto)
    target_compile_options(reconstructed_screen_item_tests PRIVATE
        -fno-lto -Wall -Wextra -Wpedantic -Werror)
    target_link_options(reconstructed_screen_item_tests PRIVATE -fno-lto)
endif()
add_test(NAME reconstructed_screen_item COMMAND reconstructed_screen_item_tests)
set_tests_properties(reconstructed_screen_item PROPERTIES
    LABELS "reconstructed;headless" TIMEOUT 30)

# One source closure connects actual screen insertion to name lookup. The
# generated fixture owns its allocation and deterministic platform providers.
add_library(beachlife_reconstructed_screen_registry STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/startup_list_node.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_list_insert.cpp"
    "${BEACHLIFE_DECOMP_PREPARED}/beachlife-decomp/support/startup_list_begin.cpp"
    "${BEACHLIFE_DECOMP_PREPARED}/beachlife-decomp/support/startup_list_end.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/pool_chunk_acquire.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/pool_return.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/pool_lock_acquire.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/pool_lock_guard.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_name_search.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_constructor.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_destructor.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_prepare.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_position.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_move.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_prepare.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_position.cpp")
target_include_directories(beachlife_reconstructed_screen_registry PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/providers/pool_lock"
    "${BEACHLIFE_DECOMP_SOURCE}")
target_compile_definitions(beachlife_reconstructed_screen_registry PUBLIC
    BEACHLIFE_DECOMP_TEST_OBJECTS=1)
target_compile_features(beachlife_reconstructed_screen_registry PUBLIC cxx_std_20)
add_dependencies(beachlife_reconstructed_screen_registry verify_reconstructed_source)

add_executable(reconstructed_screen_registry_tests tests/reconstructed_screen_registry.cpp)
target_link_libraries(reconstructed_screen_registry_tests PRIVATE
    beachlife_reconstructed_screen_registry)
set_target_properties(beachlife_reconstructed_screen_registry reconstructed_screen_registry_tests
    PROPERTIES CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
if(MSVC)
    target_compile_options(beachlife_reconstructed_screen_registry PRIVATE /GL-)
    target_compile_options(reconstructed_screen_registry_tests PRIVATE /GL- /W4 /WX)
else()
    target_compile_options(beachlife_reconstructed_screen_registry PRIVATE -fno-lto)
    target_compile_options(reconstructed_screen_registry_tests PRIVATE
        -fno-lto -Wall -Wextra -Wpedantic -Werror)
    target_link_options(reconstructed_screen_registry_tests PRIVATE -fno-lto)
endif()
add_test(NAME reconstructed_screen_registry COMMAND reconstructed_screen_registry_tests)
set_tests_properties(reconstructed_screen_registry PROPERTIES
    LABELS "reconstructed;headless" TIMEOUT 30)

# Actual saved-placement allocation, insertion, writes, manager saves and
# lookup share one source closure with generated prefix/ownership fixtures.
add_library(beachlife_reconstructed_screen_placements STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/startup_placement_node.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_placement_insert.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_placement_write.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_placement_save.cpp"
    "${BEACHLIFE_DECOMP_PREPARED}/beachlife-decomp/support/startup_list_begin.cpp"
    "${BEACHLIFE_DECOMP_PREPARED}/beachlife-decomp/support/startup_list_end.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/pool_chunk_acquire.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/pool_return.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/pool_lock_acquire.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/pool_lock_guard.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_placement.cpp")
target_include_directories(beachlife_reconstructed_screen_placements PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/providers/pool_lock"
    "${BEACHLIFE_DECOMP_SOURCE}")
target_compile_features(beachlife_reconstructed_screen_placements PUBLIC cxx_std_20)
add_dependencies(beachlife_reconstructed_screen_placements verify_reconstructed_source)

add_executable(reconstructed_screen_placements_tests tests/reconstructed_screen_placements.cpp)
target_link_libraries(reconstructed_screen_placements_tests PRIVATE
    beachlife_reconstructed_screen_placements)
set_target_properties(beachlife_reconstructed_screen_placements reconstructed_screen_placements_tests
    PROPERTIES CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
if(MSVC)
    target_compile_options(beachlife_reconstructed_screen_placements PRIVATE /GL-)
    target_compile_options(reconstructed_screen_placements_tests PRIVATE /GL- /W4 /WX)
else()
    target_compile_options(beachlife_reconstructed_screen_placements PRIVATE -fno-lto)
    target_compile_options(reconstructed_screen_placements_tests PRIVATE
        -fno-lto -Wall -Wextra -Wpedantic -Werror)
    target_link_options(reconstructed_screen_placements_tests PRIVATE -fno-lto)
endif()
add_test(NAME reconstructed_screen_placements COMMAND reconstructed_screen_placements_tests)
set_tests_properties(reconstructed_screen_placements PROPERTIES
    LABELS "reconstructed;headless" TIMEOUT 30)

# The actual GUI integer reader uses a generated guarded prefix and byte fixture.
add_library(beachlife_reconstructed_gui_integer STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_integer.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_whitespace.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_text.cpp")
target_include_directories(beachlife_reconstructed_gui_integer PUBLIC
    "${BEACHLIFE_DECOMP_SOURCE}")
target_compile_features(beachlife_reconstructed_gui_integer PUBLIC cxx_std_20)
add_dependencies(beachlife_reconstructed_gui_integer verify_reconstructed_source)

add_executable(reconstructed_gui_integer_tests tests/reconstructed_gui_integer.cpp)
target_link_libraries(reconstructed_gui_integer_tests PRIVATE
    beachlife_reconstructed_gui_integer)
set_target_properties(beachlife_reconstructed_gui_integer reconstructed_gui_integer_tests
    PROPERTIES CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
if(MSVC)
    target_compile_options(beachlife_reconstructed_gui_integer PRIVATE /GL-)
    target_compile_options(reconstructed_gui_integer_tests PRIVATE /GL- /W4 /WX)
else()
    target_compile_options(beachlife_reconstructed_gui_integer PRIVATE -fno-lto)
    target_compile_options(reconstructed_gui_integer_tests PRIVATE
        -fno-lto -Wall -Wextra -Wpedantic -Werror)
    target_link_options(reconstructed_gui_integer_tests PRIVATE -fno-lto)
endif()
add_test(NAME reconstructed_gui_integer COMMAND reconstructed_gui_integer_tests)
set_tests_properties(reconstructed_gui_integer PROPERTIES
    LABELS "reconstructed;headless" TIMEOUT 30)


# The sequential fixture selects both real helpers from the same GUI archive.
add_executable(reconstructed_gui_whitespace_tests tests/reconstructed_gui_whitespace.cpp)
target_link_libraries(reconstructed_gui_whitespace_tests PRIVATE
    beachlife_reconstructed_gui_integer)
set_target_properties(reconstructed_gui_whitespace_tests PROPERTIES
    CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
if(MSVC)
    target_compile_options(reconstructed_gui_whitespace_tests PRIVATE /GL- /W4 /WX)
else()
    target_compile_options(reconstructed_gui_whitespace_tests PRIVATE
        -fno-lto -Wall -Wextra -Wpedantic -Werror)
    target_link_options(reconstructed_gui_whitespace_tests PRIVATE -fno-lto)
endif()
add_test(NAME reconstructed_gui_whitespace COMMAND reconstructed_gui_whitespace_tests)
set_tests_properties(reconstructed_gui_whitespace PROPERTIES
    LABELS "reconstructed;headless" TIMEOUT 30)


# The text fixture selects all three actual readers from the existing GUI archive.
add_executable(reconstructed_gui_text_tests tests/reconstructed_gui_text.cpp)
target_link_libraries(reconstructed_gui_text_tests PRIVATE
    beachlife_reconstructed_gui_integer)
set_target_properties(reconstructed_gui_text_tests PROPERTIES
    CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
if(MSVC)
    target_compile_options(reconstructed_gui_text_tests PRIVATE /GL- /W4 /WX)
else()
    target_compile_options(reconstructed_gui_text_tests PRIVATE
        -fno-lto -Wall -Wextra -Wpedantic -Werror)
    target_link_options(reconstructed_gui_text_tests PRIVATE -fno-lto)
endif()
add_test(NAME reconstructed_gui_text COMMAND reconstructed_gui_text_tests)
set_tests_properties(reconstructed_gui_text PROPERTIES
    LABELS "reconstructed;headless" TIMEOUT 30)

# Bounded rectangle parsing uses the real reporter and generated parent objects.
# Whitespace is selected once from the existing shared GUI reader archive.
add_library(beachlife_reconstructed_gui_rectangle STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_rectangle.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_rectangle.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_fatal.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_constructor.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_destructor.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_prepare.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_position.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_move.cpp")
target_include_directories(beachlife_reconstructed_gui_rectangle PUBLIC
    "${BEACHLIFE_DECOMP_SOURCE}")
target_compile_features(beachlife_reconstructed_gui_rectangle PUBLIC cxx_std_20)
target_link_libraries(beachlife_reconstructed_gui_rectangle PUBLIC
    beachlife_reconstructed_gui_integer)
add_dependencies(beachlife_reconstructed_gui_rectangle verify_reconstructed_source)

add_executable(reconstructed_gui_rectangle_tests tests/reconstructed_gui_rectangle.cpp)
target_link_libraries(reconstructed_gui_rectangle_tests PRIVATE
    beachlife_reconstructed_gui_rectangle)
set_target_properties(beachlife_reconstructed_gui_rectangle reconstructed_gui_rectangle_tests
    PROPERTIES CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
if(MSVC)
    target_compile_options(beachlife_reconstructed_gui_rectangle PRIVATE /GL-)
    target_compile_options(reconstructed_gui_rectangle_tests PRIVATE /GL- /W4 /WX)
else()
    target_compile_options(beachlife_reconstructed_gui_rectangle PRIVATE -fno-lto)
    target_compile_options(reconstructed_gui_rectangle_tests PRIVATE
        -fno-lto -Wall -Wextra -Wpedantic -Werror)
    target_link_options(reconstructed_gui_rectangle_tests PRIVATE -fno-lto)
endif()
add_test(NAME reconstructed_gui_rectangle COMMAND reconstructed_gui_rectangle_tests)
set_tests_properties(reconstructed_gui_rectangle PROPERTIES
    LABELS "reconstructed;headless" TIMEOUT 30)
foreach(BEACHLIFE_RECTANGLE_FATAL_CASE IN ITEMS fatal_right fatal_bottom fatal_reversed)
    add_test(NAME reconstructed_gui_rectangle_${BEACHLIFE_RECTANGLE_FATAL_CASE}
        COMMAND reconstructed_gui_rectangle_tests --fatal ${BEACHLIFE_RECTANGLE_FATAL_CASE})
    set_tests_properties(reconstructed_gui_rectangle_${BEACHLIFE_RECTANGLE_FATAL_CASE}
        PROPERTIES LABELS "reconstructed;headless" TIMEOUT 30)
endforeach()

# Experimental reconstructed methods are opt-in and separate from strict matching.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    # Context history borrows real generated items and uses the actual link lifetime.
    add_library(beachlife_reconstructed_gui_context STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_context.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_child_link_constructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_child_link_destructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_constructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_destructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_prepare.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_position.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_move.cpp")
    target_include_directories(beachlife_reconstructed_gui_context PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_gui_context PUBLIC cxx_std_20)
    add_dependencies(beachlife_reconstructed_gui_context verify_reconstructed_source)

    add_executable(reconstructed_gui_context_tests tests/reconstructed_gui_context.cpp)
    target_link_libraries(reconstructed_gui_context_tests PRIVATE
        beachlife_reconstructed_gui_context)
    set_target_properties(beachlife_reconstructed_gui_context reconstructed_gui_context_tests
        PROPERTIES CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_gui_context PRIVATE /GL-)
        target_compile_options(reconstructed_gui_context_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_gui_context PRIVATE -fno-lto)
        target_compile_options(reconstructed_gui_context_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_gui_context_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_gui_context COMMAND reconstructed_gui_context_tests)
    set_tests_properties(reconstructed_gui_context PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# The full TGA parser is available only in the experimental prepared profile.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    target_sources(beachlife_reconstructed_tga PRIVATE
        "${BEACHLIFE_DECOMP_SOURCE}/tga_image_allocation.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/tga_buffer_parser.cpp")
    add_executable(reconstructed_tga_parser_tests tests/reconstructed_tga_parser.cpp)
    target_link_libraries(reconstructed_tga_parser_tests PRIVATE beachlife_reconstructed_tga)
    set_target_properties(beachlife_reconstructed_tga reconstructed_tga_parser_tests
        PROPERTIES CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_tga PRIVATE /GL-)
        target_compile_options(reconstructed_tga_parser_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_tga PRIVATE -fno-lto)
        target_compile_options(reconstructed_tga_parser_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_tga_parser_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_tga_parser COMMAND reconstructed_tga_parser_tests)
    set_tests_properties(reconstructed_tga_parser PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 60)
endif()

# The filename wrapper uses the real reconstructed parser and scoped VFS helpers.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_tga_files STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/tga_file_loader.cpp")
    target_include_directories(beachlife_reconstructed_tga_files PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_tga_files PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_tga_files PUBLIC
        beachlife_reconstructed_tga beachlife_reconstructed_files)
    add_dependencies(beachlife_reconstructed_tga_files verify_reconstructed_source)

    add_executable(reconstructed_tga_files_tests tests/reconstructed_tga_files.cpp)
    target_link_libraries(reconstructed_tga_files_tests PRIVATE
        beachlife_reconstructed_tga_files)
    set_target_properties(beachlife_reconstructed_tga_files reconstructed_tga_files_tests
        PROPERTIES CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_tga_files PRIVATE /GL-)
        target_compile_options(reconstructed_tga_files_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_tga_files PRIVATE -fno-lto)
        target_compile_options(reconstructed_tga_files_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_tga_files_tests PRIVATE -fno-lto)
    endif()
    set(BEACHLIFE_RECONSTRUCTED_TGA_FILES_ROOT
        "${CMAKE_CURRENT_BINARY_DIR}/tga-files-fixture"
        CACHE STRING "Writable target-system directory for generated TGA file tests")
    add_test(NAME reconstructed_tga_files COMMAND reconstructed_tga_files_tests
        "${BEACHLIFE_RECONSTRUCTED_TGA_FILES_ROOT}")
    set_tests_properties(reconstructed_tga_files PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 60)
endif()

# Append after the existing optional reconstructed-test gate in Decomp.cmake.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_indexed_palette_blit STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/indexed_palette_blit.cpp")
    target_include_directories(beachlife_reconstructed_indexed_palette_blit PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_indexed_palette_blit PUBLIC cxx_std_20)
    add_dependencies(beachlife_reconstructed_indexed_palette_blit verify_reconstructed_source)

    add_executable(reconstructed_indexed_palette_blit_tests
        tests/reconstructed_indexed_palette_blit.cpp)
    target_link_libraries(reconstructed_indexed_palette_blit_tests PRIVATE
        beachlife_reconstructed_indexed_palette_blit)
    set_target_properties(beachlife_reconstructed_indexed_palette_blit
        reconstructed_indexed_palette_blit_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_indexed_palette_blit PRIVATE /GL-)
        target_compile_options(reconstructed_indexed_palette_blit_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_indexed_palette_blit PRIVATE -fno-lto)
        target_compile_options(reconstructed_indexed_palette_blit_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_indexed_palette_blit_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_indexed_palette_blit COMMAND reconstructed_indexed_palette_blit_tests)
    set_tests_properties(reconstructed_indexed_palette_blit PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Context pop shares the actual push and item/link lifetime implementation.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    target_sources(beachlife_reconstructed_gui_context PRIVATE
        "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_pop.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_fatal.cpp")
    add_executable(reconstructed_gui_pop_tests tests/reconstructed_gui_pop.cpp)
    target_link_libraries(reconstructed_gui_pop_tests PRIVATE
        beachlife_reconstructed_gui_context)
    set_target_properties(reconstructed_gui_pop_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(reconstructed_gui_pop_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(reconstructed_gui_pop_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_gui_pop_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_gui_pop COMMAND reconstructed_gui_pop_tests)
    add_test(NAME reconstructed_gui_pop_fatal_empty
        COMMAND reconstructed_gui_pop_tests --fatal-empty)
    add_test(NAME reconstructed_gui_pop_fatal_history
        COMMAND reconstructed_gui_pop_tests --fatal-history)
    set_tests_properties(reconstructed_gui_pop reconstructed_gui_pop_fatal_empty
        reconstructed_gui_pop_fatal_history PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Palette conversion shares the existing prepared TGA lifetime owners.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    target_sources(beachlife_reconstructed_tga PRIVATE
        "${BEACHLIFE_DECOMP_SOURCE}/tga_palette_convert.cpp")
    add_executable(reconstructed_tga_palette_tests tests/reconstructed_tga_palette.cpp)
    target_link_libraries(reconstructed_tga_palette_tests PRIVATE beachlife_reconstructed_tga)
    set_target_properties(reconstructed_tga_palette_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(reconstructed_tga_palette_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(reconstructed_tga_palette_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_tga_palette_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_tga_palette COMMAND reconstructed_tga_palette_tests)
    set_tests_properties(reconstructed_tga_palette PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Registration shares the real context, link and screen-item lifetime owners.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    target_sources(beachlife_reconstructed_gui_context PRIVATE
        "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_register.cpp")
    add_executable(reconstructed_gui_registration_tests
        tests/reconstructed_gui_registration.cpp)
    target_link_libraries(reconstructed_gui_registration_tests PRIVATE
        beachlife_reconstructed_gui_context)
    set_target_properties(reconstructed_gui_registration_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(reconstructed_gui_registration_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(reconstructed_gui_registration_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_gui_registration_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_gui_registration COMMAND reconstructed_gui_registration_tests)
    set_tests_properties(reconstructed_gui_registration PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# These three call-free kernels complete the indexed DrawTga kernel family.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_renderer_blit_kernels STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/indexed_palette_blit_transparent.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/indexed_palette_blit_packed.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/indexed_palette_blit_masked.cpp")
    target_include_directories(beachlife_reconstructed_renderer_blit_kernels PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_renderer_blit_kernels PUBLIC cxx_std_20)
    add_dependencies(beachlife_reconstructed_renderer_blit_kernels verify_reconstructed_source)

    add_executable(reconstructed_renderer_blit_kernels_tests
        tests/reconstructed_renderer_blit_kernels.cpp)
    target_link_libraries(reconstructed_renderer_blit_kernels_tests PRIVATE
        beachlife_reconstructed_renderer_blit_kernels)
    set_target_properties(beachlife_reconstructed_renderer_blit_kernels
        reconstructed_renderer_blit_kernels_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_renderer_blit_kernels PRIVATE /GL-)
        target_compile_options(reconstructed_renderer_blit_kernels_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_renderer_blit_kernels PRIVATE -fno-lto)
        target_compile_options(reconstructed_renderer_blit_kernels_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_renderer_blit_kernels_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_renderer_blit_kernels
        COMMAND reconstructed_renderer_blit_kernels_tests)
    set_tests_properties(reconstructed_renderer_blit_kernels PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# One real activation closure supplies the concrete base slot to its consumers.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_screen_activation STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_activation.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_manager_item_clear.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_manager_auxiliary_clear.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_intro_globals.cpp")
    target_include_directories(beachlife_reconstructed_screen_activation PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_screen_activation PUBLIC cxx_std_20)
    target_compile_definitions(beachlife_reconstructed_screen_activation PUBLIC
        BEACHLIFE_DECOMP_SCREEN_ACTIVATION=1)
    add_dependencies(beachlife_reconstructed_screen_activation verify_reconstructed_source)

    foreach(BEACHLIFE_ACTIVATION_CONSUMER IN ITEMS
        beachlife_reconstructed_screen_item
        beachlife_reconstructed_screen_registry
        beachlife_reconstructed_gui_rectangle
        beachlife_reconstructed_gui_context)
        target_link_libraries(${BEACHLIFE_ACTIVATION_CONSUMER} PUBLIC
            beachlife_reconstructed_screen_activation)
    endforeach()
    # These aggregate-only consumers need the expanded field initialization,
    # without pulling any activation or base-lifetime owner into their images.
    foreach(BEACHLIFE_ACTIVATION_PREFIX_CONSUMER IN ITEMS
        beachlife_reconstructed_gui_integer
        beachlife_reconstructed_screen_placements)
        target_compile_definitions(${BEACHLIFE_ACTIVATION_PREFIX_CONSUMER} PUBLIC
            BEACHLIFE_DECOMP_SCREEN_ACTIVATION=1)
    endforeach()

    add_executable(reconstructed_screen_activation_tests
        tests/reconstructed_screen_activation.cpp)
    target_link_libraries(reconstructed_screen_activation_tests PRIVATE
        beachlife_reconstructed_screen_item)
    set_target_properties(beachlife_reconstructed_screen_activation
        reconstructed_screen_activation_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_screen_activation PRIVATE /GL-)
        target_compile_options(reconstructed_screen_activation_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_screen_activation PRIVATE -fno-lto)
        target_compile_options(reconstructed_screen_activation_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_screen_activation_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_screen_activation COMMAND reconstructed_screen_activation_tests)
    set_tests_properties(reconstructed_screen_activation PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# The experimental renderer uses native value storage for its dirty rectangles.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    # Only fixture setup/snapshots depend on this definition. The prepared
    # renderer declaration is identical in every translation unit of an image.
    foreach(BEACHLIFE_RENDERER_QUEUE_CONSUMER IN ITEMS
        beachlife_reconstructed_surfaces
        beachlife_reconstructed_gui_rectangle)
        target_compile_definitions(${BEACHLIFE_RENDERER_QUEUE_CONSUMER} PUBLIC
            BEACHLIFE_DECOMP_RENDERER_QUEUE=1)
    endforeach()

    add_library(beachlife_reconstructed_renderer_rectangle_queue STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_rectangle_accumulate.cpp")
    target_include_directories(beachlife_reconstructed_renderer_rectangle_queue PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_renderer_rectangle_queue PUBLIC cxx_std_20)
    add_dependencies(beachlife_reconstructed_renderer_rectangle_queue verify_reconstructed_source)

    add_executable(reconstructed_renderer_rectangle_queue_tests
        tests/reconstructed_renderer_rectangle_queue.cpp)
    target_link_libraries(reconstructed_renderer_rectangle_queue_tests PRIVATE
        beachlife_reconstructed_renderer_rectangle_queue)
    set_target_properties(beachlife_reconstructed_renderer_rectangle_queue
        reconstructed_renderer_rectangle_queue_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_renderer_rectangle_queue PRIVATE /GL-)
        target_compile_options(reconstructed_renderer_rectangle_queue_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_renderer_rectangle_queue PRIVATE -fno-lto)
        target_compile_options(reconstructed_renderer_rectangle_queue_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_renderer_rectangle_queue_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_renderer_rectangle_queue
        COMMAND reconstructed_renderer_rectangle_queue_tests)
    set_tests_properties(reconstructed_renderer_rectangle_queue PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# The clip method uses the actual native rectangle accumulator and value queue.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_renderer_clip STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_clip.cpp")
    target_include_directories(beachlife_reconstructed_renderer_clip PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_renderer_clip PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_renderer_clip PUBLIC
        beachlife_reconstructed_renderer_rectangle_queue)
    add_dependencies(beachlife_reconstructed_renderer_clip verify_reconstructed_source)
    add_executable(reconstructed_renderer_clip_tests tests/reconstructed_renderer_clip.cpp)
    target_link_libraries(reconstructed_renderer_clip_tests PRIVATE
        beachlife_reconstructed_renderer_clip)
    set_target_properties(beachlife_reconstructed_renderer_clip
        reconstructed_renderer_clip_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_renderer_clip PRIVATE /GL-)
        target_compile_options(reconstructed_renderer_clip_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_renderer_clip PRIVATE -fno-lto)
        target_compile_options(reconstructed_renderer_clip_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_renderer_clip_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_renderer_clip COMMAND reconstructed_renderer_clip_tests)
    set_tests_properties(reconstructed_renderer_clip PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Complete drawing composes actual clipping, pixels, kernels and image lifetime.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_renderer_tga_draw STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_tga_draw.cpp")
    target_include_directories(beachlife_reconstructed_renderer_tga_draw PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_renderer_tga_draw PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_renderer_tga_draw PUBLIC
        beachlife_reconstructed_renderer_clip
        beachlife_reconstructed_indexed_palette_blit
        beachlife_reconstructed_renderer_blit_kernels
        beachlife_reconstructed_surface_pixels)
    add_dependencies(beachlife_reconstructed_renderer_tga_draw verify_reconstructed_source)

    add_executable(reconstructed_renderer_tga_draw_tests
        tests/reconstructed_renderer_tga_draw.cpp)
    target_link_libraries(reconstructed_renderer_tga_draw_tests PRIVATE
        beachlife_reconstructed_renderer_tga_draw beachlife_reconstructed_tga)
    set_target_properties(beachlife_reconstructed_renderer_tga_draw
        reconstructed_renderer_tga_draw_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_renderer_tga_draw PRIVATE /GL-)
        target_compile_options(reconstructed_renderer_tga_draw_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_renderer_tga_draw PRIVATE -fno-lto)
        target_compile_options(reconstructed_renderer_tga_draw_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_renderer_tga_draw_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_renderer_tga_draw COMMAND reconstructed_renderer_tga_draw_tests)
    set_tests_properties(reconstructed_renderer_tga_draw PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()


# The fill method uses the actual pixel-access/error-sink source and COM boundary.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_renderer_rectangle_fill STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_rectangle_fill.cpp")
    target_include_directories(beachlife_reconstructed_renderer_rectangle_fill PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_renderer_rectangle_fill PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_renderer_rectangle_fill PUBLIC
        beachlife_reconstructed_surface_pixels)
    add_dependencies(beachlife_reconstructed_renderer_rectangle_fill verify_reconstructed_source)

    add_executable(reconstructed_renderer_rectangle_fill_tests
        tests/reconstructed_renderer_rectangle_fill.cpp)
    target_link_libraries(reconstructed_renderer_rectangle_fill_tests PRIVATE
        beachlife_reconstructed_renderer_rectangle_fill)
    set_target_properties(beachlife_reconstructed_renderer_rectangle_fill
        reconstructed_renderer_rectangle_fill_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_renderer_rectangle_fill PRIVATE /GL-)
        target_compile_options(reconstructed_renderer_rectangle_fill_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_renderer_rectangle_fill PRIVATE -fno-lto)
        target_compile_options(reconstructed_renderer_rectangle_fill_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_renderer_rectangle_fill_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_renderer_rectangle_fill
        COMMAND reconstructed_renderer_rectangle_fill_tests)
    set_tests_properties(reconstructed_renderer_rectangle_fill PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()


# These frame helpers use the actual fill/converter and existing renderer owner.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_frame_fill_styles STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_colour.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_frame_style2.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_frame_style3.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_frame_style7.cpp")
    target_include_directories(beachlife_reconstructed_frame_fill_styles PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_frame_fill_styles PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_frame_fill_styles PUBLIC
        beachlife_reconstructed_renderer_rectangle_fill
        beachlife_reconstructed_gui_rectangle)
    add_dependencies(beachlife_reconstructed_frame_fill_styles verify_reconstructed_source)

    add_executable(reconstructed_frame_fill_styles_tests tests/reconstructed_frame_fill_styles.cpp)
    target_link_libraries(reconstructed_frame_fill_styles_tests PRIVATE
        beachlife_reconstructed_frame_fill_styles)
    set_target_properties(beachlife_reconstructed_frame_fill_styles
        reconstructed_frame_fill_styles_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_frame_fill_styles PRIVATE /GL-)
        target_compile_options(reconstructed_frame_fill_styles_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_frame_fill_styles PRIVATE -fno-lto)
        target_compile_options(reconstructed_frame_fill_styles_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_frame_fill_styles_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_frame_fill_styles COMMAND reconstructed_frame_fill_styles_tests)
    set_tests_properties(reconstructed_frame_fill_styles PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()


# Experimental font measurement borrows genuinely constructed font data.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_font_measurement STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_font_renderer_constructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_font_measure_text.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_font_count_wrapped_lines.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_font_measure_wrapped_text.cpp")
    target_include_directories(beachlife_reconstructed_font_measurement PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_font_measurement PUBLIC cxx_std_20)
    # Retain the sole actual renderer global owner and its real linked children.
    target_link_libraries(beachlife_reconstructed_font_measurement PUBLIC
        beachlife_reconstructed_gui_rectangle)
    add_dependencies(beachlife_reconstructed_font_measurement verify_reconstructed_source)

    add_executable(reconstructed_font_measurement_tests
        tests/reconstructed_font_measurement.cpp)
    target_link_libraries(reconstructed_font_measurement_tests PRIVATE
        beachlife_reconstructed_font_measurement)
    set_target_properties(beachlife_reconstructed_font_measurement
        reconstructed_font_measurement_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_font_measurement PRIVATE /GL-)
        target_compile_options(reconstructed_font_measurement_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_font_measurement PRIVATE -fno-lto)
        target_compile_options(reconstructed_font_measurement_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_font_measurement_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_font_measurement
        COMMAND reconstructed_font_measurement_tests
            "${CMAKE_CURRENT_BINARY_DIR}/generated/font-measurement-font-owner")
    set_tests_properties(reconstructed_font_measurement PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()


# Real line rasterization and Style6 share the existing colour and renderer owners.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_renderer_line STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_line_direct_x.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_line_direct_y.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_line_packed_x.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_line_packed_y.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_line.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_frame_style6.cpp")
    target_include_directories(beachlife_reconstructed_renderer_line PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_renderer_line PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_renderer_line PUBLIC
        beachlife_reconstructed_frame_fill_styles)
    add_dependencies(beachlife_reconstructed_renderer_line verify_reconstructed_source)

    add_executable(reconstructed_renderer_line_tests tests/reconstructed_renderer_line.cpp)
    target_link_libraries(reconstructed_renderer_line_tests PRIVATE
        beachlife_reconstructed_renderer_line)
    set_target_properties(beachlife_reconstructed_renderer_line
        reconstructed_renderer_line_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_renderer_line PRIVATE /GL-)
        target_compile_options(reconstructed_renderer_line_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_renderer_line PRIVATE -fno-lto)
        target_compile_options(reconstructed_renderer_line_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_renderer_line_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_renderer_line COMMAND reconstructed_renderer_line_tests)
    set_tests_properties(reconstructed_renderer_line PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Experimental localization uses genuine language owners and scoped borrowed method views.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_screen_localization STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_text_find.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_text_end.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_text_lookup.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_text_empty.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_text_globals.cpp")
    target_include_directories(beachlife_reconstructed_screen_localization PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_screen_localization PUBLIC cxx_std_20)
    add_dependencies(beachlife_reconstructed_screen_localization verify_reconstructed_source)

    add_executable(reconstructed_screen_localization_tests
        tests/reconstructed_screen_localization.cpp)
    target_link_libraries(reconstructed_screen_localization_tests PRIVATE
        beachlife_reconstructed_screen_localization)
    set_target_properties(beachlife_reconstructed_screen_localization
        reconstructed_screen_localization_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_screen_localization PRIVATE /GL-)
        target_compile_options(reconstructed_screen_localization_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_screen_localization PRIVATE -fno-lto)
        target_compile_options(reconstructed_screen_localization_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_screen_localization_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_screen_localization COMMAND reconstructed_screen_localization_tests)
    set_tests_properties(reconstructed_screen_localization PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Auxiliary text shares the real lookup, font and manager global owners.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    # Prepared declarations are identical in every TU. This definition only
    # selects typed fixture initialization/snapshots for the refined prefixes.
    foreach(BEACHLIFE_AUXILIARY_PREFIX_CONSUMER IN ITEMS
        beachlife_reconstructed_screen_activation
        beachlife_reconstructed_gui_integer
        beachlife_reconstructed_screen_placements)
        target_compile_definitions(${BEACHLIFE_AUXILIARY_PREFIX_CONSUMER} PUBLIC
            BEACHLIFE_DECOMP_AUXILIARY_TEXT=1)
    endforeach()

    add_library(beachlife_reconstructed_auxiliary_text STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_manager_auxiliary_text.cpp")
    target_include_directories(beachlife_reconstructed_auxiliary_text PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_auxiliary_text PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_auxiliary_text PUBLIC
        beachlife_reconstructed_font_measurement
        beachlife_reconstructed_screen_localization
        beachlife_reconstructed_screen_activation)
    add_dependencies(beachlife_reconstructed_auxiliary_text verify_reconstructed_source)

    add_executable(reconstructed_auxiliary_text_tests tests/reconstructed_auxiliary_text.cpp)
    target_link_libraries(reconstructed_auxiliary_text_tests PRIVATE
        beachlife_reconstructed_auxiliary_text)
    set_target_properties(beachlife_reconstructed_auxiliary_text
        reconstructed_auxiliary_text_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_auxiliary_text PRIVATE /GL-)
        target_compile_options(reconstructed_auxiliary_text_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_auxiliary_text PRIVATE -fno-lto)
        target_compile_options(reconstructed_auxiliary_text_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_auxiliary_text_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_auxiliary_text
        COMMAND reconstructed_auxiliary_text_tests
            "${CMAKE_CURRENT_BINARY_DIR}/generated/auxiliary-text-font-owner")
    set_tests_properties(reconstructed_auxiliary_text PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# The full observed base table uses real methods and one coherent declaration.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_screen_item_complete STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_defaults.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_state.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_hit.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_text.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_timed_event.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_manager_threshold.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_timed_coordinates.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_manager_text.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_manager_auxiliary_position.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_frame_rectangle.cpp")
    target_include_directories(beachlife_reconstructed_screen_item_complete PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_screen_item_complete PUBLIC cxx_std_20)
    # This flag adapts generated observers only. Every retail TU consumes the
    # same complete class definition from the experimental prepared source.
    target_compile_definitions(beachlife_reconstructed_screen_item_complete PUBLIC
        BEACHLIFE_DECOMP_COMPLETE_SCREEN_ITEM=1)
    target_link_libraries(beachlife_reconstructed_screen_item_complete PUBLIC
        beachlife_reconstructed_auxiliary_text
        beachlife_reconstructed_screen_activation
        beachlife_reconstructed_frame_clock)
    add_dependencies(beachlife_reconstructed_screen_item_complete verify_reconstructed_source)

    # Genuine static dependencies form a cycle through font/rectangle/base.
    # CMake determines the repeated archive order; no whole-archive or synthetic
    # owner is introduced. Existing archives retain their actual source members.
    foreach(BEACHLIFE_COMPLETE_ITEM_CONSUMER IN ITEMS
        beachlife_reconstructed_screen_item
        beachlife_reconstructed_screen_registry
        beachlife_reconstructed_gui_rectangle
        beachlife_reconstructed_gui_context)
        target_link_libraries(${BEACHLIFE_COMPLETE_ITEM_CONSUMER} PUBLIC
            beachlife_reconstructed_screen_item_complete)
    endforeach()

    add_executable(reconstructed_screen_item_complete_tests
        tests/reconstructed_screen_item_complete.cpp)
    target_link_libraries(reconstructed_screen_item_complete_tests PRIVATE
        beachlife_reconstructed_screen_item_complete)
    set_target_properties(beachlife_reconstructed_screen_item_complete
        reconstructed_screen_item_complete_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_screen_item_complete PRIVATE /GL-)
        target_compile_options(reconstructed_screen_item_complete_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_screen_item_complete PRIVATE -fno-lto)
        target_compile_options(reconstructed_screen_item_complete_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_screen_item_complete_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_screen_item_complete
        COMMAND reconstructed_screen_item_complete_tests
            "${CMAKE_CURRENT_BINARY_DIR}/generated/screen-item-complete-font-owner")
    set_tests_properties(reconstructed_screen_item_complete PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# A real TGA widget owns the image loaded through the existing VFS bridge.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_tga_widget STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_tga_screen_item_constructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_tga_screen_item_draw.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_tga_screen_item_activation.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_tga_screen_item_destructor.cpp")
    target_include_directories(beachlife_reconstructed_tga_widget PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_tga_widget PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_tga_widget PUBLIC
        beachlife_reconstructed_screen_item_complete
        beachlife_reconstructed_tga_files
        beachlife_reconstructed_renderer_tga_draw)
    add_dependencies(beachlife_reconstructed_tga_widget verify_reconstructed_source)

    add_executable(reconstructed_tga_widget_tests tests/reconstructed_tga_widget.cpp)
    target_link_libraries(reconstructed_tga_widget_tests PRIVATE
        beachlife_reconstructed_tga_widget)
    set_target_properties(beachlife_reconstructed_tga_widget
        reconstructed_tga_widget_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_tga_widget PRIVATE /GL-)
        target_compile_options(reconstructed_tga_widget_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_tga_widget PRIVATE -fno-lto)
        target_compile_options(reconstructed_tga_widget_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_tga_widget_tests PRIVATE -fno-lto)
    endif()
    set(BEACHLIFE_RECONSTRUCTED_TGA_WIDGET_ROOT "${CMAKE_CURRENT_BINARY_DIR}/tga-widget-fixture"
        CACHE STRING "Writable target-system directory for generated TGA widget files")
    add_test(NAME reconstructed_tga_widget COMMAND reconstructed_tga_widget_tests
        "${BEACHLIFE_RECONSTRUCTED_TGA_WIDGET_ROOT}")
    set_tests_properties(reconstructed_tga_widget PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Resource metadata uses real frame lifetimes and CRT binary streams.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_resource_frame_metadata STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_frame_constructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_frame_destructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_frame_metadata.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_resource_read_byte.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_resource_read_word.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_resource_read_dword.cpp")
    target_include_directories(beachlife_reconstructed_resource_frame_metadata PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_resource_frame_metadata PUBLIC cxx_std_20)
    add_dependencies(beachlife_reconstructed_resource_frame_metadata verify_reconstructed_source)

    add_executable(reconstructed_resource_frame_metadata_tests
        tests/reconstructed_resource_frame_metadata.cpp)
    target_link_libraries(reconstructed_resource_frame_metadata_tests PRIVATE
        beachlife_reconstructed_resource_frame_metadata)
    set_target_properties(beachlife_reconstructed_resource_frame_metadata
        reconstructed_resource_frame_metadata_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_resource_frame_metadata PRIVATE /GL-)
        target_compile_options(reconstructed_resource_frame_metadata_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_resource_frame_metadata PRIVATE -fno-lto)
        target_compile_options(reconstructed_resource_frame_metadata_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_resource_frame_metadata_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_resource_frame_metadata
        COMMAND reconstructed_resource_frame_metadata_tests)
    set_tests_properties(reconstructed_resource_frame_metadata PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Resource lookup uses real wrapper lifetimes and borrowed generated records.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_resource_find STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_resource_find.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_resource_literals.cpp")
    target_include_directories(beachlife_reconstructed_resource_find PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_resource_find PUBLIC cxx_std_20)
    add_dependencies(beachlife_reconstructed_resource_find verify_reconstructed_source)

    add_executable(reconstructed_resource_find_tests
        tests/reconstructed_resource_find.cpp
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_resource_lookup.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_resource_constructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_resource_destructor.cpp")
    target_link_libraries(reconstructed_resource_find_tests PRIVATE
        beachlife_reconstructed_resource_find)
    add_dependencies(reconstructed_resource_find_tests verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_resource_find
        reconstructed_resource_find_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_resource_find PRIVATE /GL-)
        target_compile_options(reconstructed_resource_find_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_resource_find PRIVATE -fno-lto)
        target_compile_options(reconstructed_resource_find_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_resource_find_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_resource_find COMMAND reconstructed_resource_find_tests
        "${CMAKE_CURRENT_BINARY_DIR}/generated/resource-find-owner")
    set_tests_properties(reconstructed_resource_find PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Resource records own names, palettes and genuine resource Frame lifetimes.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_resource_record_lifetime STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_record_constructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_record_destructor.cpp")
    target_include_directories(beachlife_reconstructed_resource_record_lifetime PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_resource_record_lifetime PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_resource_record_lifetime PUBLIC
        beachlife_reconstructed_resource_frame_metadata)
    add_dependencies(beachlife_reconstructed_resource_record_lifetime verify_reconstructed_source)

    add_executable(reconstructed_resource_record_lifetime_tests
        tests/reconstructed_resource_record_lifetime.cpp)
    target_link_libraries(reconstructed_resource_record_lifetime_tests PRIVATE
        beachlife_reconstructed_resource_record_lifetime)
    target_link_libraries(reconstructed_resource_find_tests PRIVATE
        beachlife_reconstructed_resource_record_lifetime)
    set_target_properties(beachlife_reconstructed_resource_record_lifetime
        reconstructed_resource_record_lifetime_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_resource_record_lifetime PRIVATE /GL-)
        target_compile_options(reconstructed_resource_record_lifetime_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_resource_record_lifetime PRIVATE -fno-lto)
        target_compile_options(reconstructed_resource_record_lifetime_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_resource_record_lifetime_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_resource_record_lifetime
        COMMAND reconstructed_resource_record_lifetime_tests)
    set_tests_properties(reconstructed_resource_record_lifetime PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Resource record metadata uses the real Record and Frame lifetime archives.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_resource_record_metadata STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_record_metadata.cpp")
    target_include_directories(beachlife_reconstructed_resource_record_metadata PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_resource_record_metadata PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_resource_record_metadata PUBLIC
        beachlife_reconstructed_resource_record_lifetime)
    add_dependencies(beachlife_reconstructed_resource_record_metadata verify_reconstructed_source)

    add_executable(reconstructed_resource_record_metadata_tests
        tests/reconstructed_resource_record_metadata.cpp)
    target_link_libraries(reconstructed_resource_record_metadata_tests PRIVATE
        beachlife_reconstructed_resource_record_metadata)
    add_dependencies(reconstructed_resource_record_metadata_tests verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_resource_record_metadata
        reconstructed_resource_record_metadata_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_resource_record_metadata PRIVATE /GL-)
        target_compile_options(reconstructed_resource_record_metadata_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_resource_record_metadata PRIVATE -fno-lto)
        target_compile_options(reconstructed_resource_record_metadata_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_resource_record_metadata_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_resource_record_metadata
        COMMAND reconstructed_resource_record_metadata_tests)
    set_tests_properties(reconstructed_resource_record_metadata PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Resource loading shares genuine Record/Frame owners and the renderer global.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_resource_frame_file STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_frame_file.cpp")
    target_include_directories(beachlife_reconstructed_resource_frame_file PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_resource_frame_file PUBLIC cxx_std_20)
    add_dependencies(beachlife_reconstructed_resource_frame_file verify_reconstructed_source)

    add_library(beachlife_reconstructed_resource_file_producer STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_resource_load.cpp")
    target_include_directories(beachlife_reconstructed_resource_file_producer PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_resource_file_producer PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_resource_file_producer PUBLIC
        beachlife_reconstructed_resource_frame_file
        beachlife_reconstructed_resource_record_metadata
        beachlife_reconstructed_gui_rectangle)
    add_dependencies(beachlife_reconstructed_resource_file_producer verify_reconstructed_source)

    # The real libraries remain portable. Only this scalar-observing fixture
    # uses the reviewed Linux Itanium ABI and GNU-compatible linker wrapping.
    set_target_properties(beachlife_reconstructed_resource_frame_file
        beachlife_reconstructed_resource_file_producer PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_resource_frame_file PRIVATE /GL-)
        target_compile_options(beachlife_reconstructed_resource_file_producer PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_resource_frame_file PRIVATE -fno-lto)
        target_compile_options(beachlife_reconstructed_resource_file_producer PRIVATE -fno-lto)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8
        AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_executable(reconstructed_resource_file_producer_tests
            tests/reconstructed_resource_file_producer.cpp)
        target_link_libraries(reconstructed_resource_file_producer_tests PRIVATE
            beachlife_reconstructed_resource_file_producer
            beachlife_reconstructed_resource_owner)
        add_dependencies(reconstructed_resource_file_producer_tests verify_reconstructed_source)
        set_target_properties(reconstructed_resource_file_producer_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_resource_file_producer_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_resource_file_producer_tests PRIVATE -fno-lto
            "LINKER:--wrap=_ZN9beachlife7runtime14resource_files11open_streamEPKcS3_"
            "LINKER:--wrap=_ZN9beachlife7runtime14resource_files11read_streamEPvmmPNS1_6StreamE"
            "LINKER:--wrap=_ZN9beachlife7runtime14resource_files12close_streamEPNS1_6StreamE"
            "LINKER:--wrap=_ZN9beachlife7runtime14resource_files15open_descriptorEPKci"
            "LINKER:--wrap=_ZN9beachlife7runtime14resource_files15read_descriptorEiPvj"
            "LINKER:--wrap=_ZN9beachlife7runtime14resource_files15seek_descriptorEiji"
            "LINKER:--wrap=_ZN9beachlife7runtime14resource_files16close_descriptorEi")
        set(BEACHLIFE_RESOURCE_FILE_TEST_ROOT
            "${CMAKE_CURRENT_BINARY_DIR}/resource-file-producer-data"
            CACHE STRING "Fresh generated-file directory for the resource producer test")
        add_test(NAME reconstructed_resource_file_producer
            COMMAND reconstructed_resource_file_producer_tests
                "${BEACHLIFE_RESOURCE_FILE_TEST_ROOT}")
        set_tests_properties(reconstructed_resource_file_producer PROPERTIES
            LABELS "reconstructed;experimental;headless" TIMEOUT 30)
    endif()
endif()

# Palette modifiers retain the actual file reader and registration lifetime.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_resource_palette_modifiers STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_palette_modifiers_load.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_palette_modifiers_path.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_palette_modifiers_constructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_palette_modifiers_destructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_palette_modifiers_registration.cpp")
    target_include_directories(beachlife_reconstructed_resource_palette_modifiers PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_resource_palette_modifiers PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_resource_palette_modifiers PUBLIC
        beachlife_reconstructed_gui_rectangle)
    add_dependencies(beachlife_reconstructed_resource_palette_modifiers verify_reconstructed_source)

    add_executable(reconstructed_resource_palette_modifiers_tests
        tests/reconstructed_resource_palette_modifiers.cpp)
    target_link_libraries(reconstructed_resource_palette_modifiers_tests PRIVATE
        beachlife_reconstructed_resource_palette_modifiers)
    set_target_properties(beachlife_reconstructed_resource_palette_modifiers
        reconstructed_resource_palette_modifiers_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_resource_palette_modifiers PRIVATE /GL-)
        target_compile_options(reconstructed_resource_palette_modifiers_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_resource_palette_modifiers PRIVATE -fno-lto)
        target_compile_options(reconstructed_resource_palette_modifiers_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_resource_palette_modifiers_tests PRIVATE -fno-lto)
    endif()
    set(BEACHLIFE_RECONSTRUCTED_RESOURCE_PALETTE_ROOT
        "${CMAKE_CURRENT_BINARY_DIR}/resource-palette-fixture"
        CACHE STRING "Generated palette-modifier fixture directory")
    add_test(NAME reconstructed_resource_palette_modifiers
        COMMAND reconstructed_resource_palette_modifiers_tests
            "${BEACHLIFE_RECONSTRUCTED_RESOURCE_PALETTE_ROOT}")
    set_tests_properties(reconstructed_resource_palette_modifiers PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Resource raw frame loading uses the actual shared descriptor and cursor.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_resource_frame_loaders STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_frame_load_all.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_frame_load_plane.cpp")
    target_include_directories(beachlife_reconstructed_resource_frame_loaders PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_resource_frame_loaders PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_resource_frame_loaders PUBLIC
        beachlife_reconstructed_resource_frame_metadata
        beachlife_reconstructed_resource_frame_file)
    add_dependencies(beachlife_reconstructed_resource_frame_loaders verify_reconstructed_source)

    add_executable(reconstructed_resource_frame_loaders_tests
        tests/reconstructed_resource_frame_loaders.cpp)
    target_link_libraries(reconstructed_resource_frame_loaders_tests PRIVATE
        beachlife_reconstructed_resource_frame_loaders)
    set_target_properties(beachlife_reconstructed_resource_frame_loaders
        reconstructed_resource_frame_loaders_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_resource_frame_loaders PRIVATE /GL-)
        target_compile_options(reconstructed_resource_frame_loaders_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_resource_frame_loaders PRIVATE -fno-lto)
        target_compile_options(reconstructed_resource_frame_loaders_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_resource_frame_loaders_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_resource_frame_loaders
        COMMAND reconstructed_resource_frame_loaders_tests
            "${CMAKE_CURRENT_BINARY_DIR}/generated/resource-frame-loaders")
    set_tests_properties(reconstructed_resource_frame_loaders PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Direct encoded sprite raster with independent bounded packet and pixel fixtures.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_executable(reconstructed_resource_raster_direct_tests
        tests/reconstructed_resource_raster_direct.cpp)
    target_link_libraries(reconstructed_resource_raster_direct_tests PRIVATE
        beachlife_reconstructed_resource_raster_direct)
    set_target_properties(reconstructed_resource_raster_direct_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(reconstructed_resource_raster_direct_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(reconstructed_resource_raster_direct_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_resource_raster_direct_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_resource_raster_direct
        COMMAND reconstructed_resource_raster_direct_tests)
    set_tests_properties(reconstructed_resource_raster_direct PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Metadata smoke inputs are explicitly supplied local files.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_executable(reconstructed_resource_metadata_smoke
        tests/reconstructed_resource_metadata_smoke.cpp)
    target_link_libraries(reconstructed_resource_metadata_smoke PRIVATE
        beachlife_reconstructed_resource_record_metadata)
    target_compile_features(reconstructed_resource_metadata_smoke PRIVATE cxx_std_20)
    add_dependencies(reconstructed_resource_metadata_smoke verify_reconstructed_source)
    set_target_properties(reconstructed_resource_metadata_smoke PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(reconstructed_resource_metadata_smoke PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(reconstructed_resource_metadata_smoke PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_resource_metadata_smoke PRIVATE -fno-lto)
    endif()
    set(BEACHLIFE_RESOURCE_METADATA_SMOKE_INPUT "" CACHE FILEPATH
        "Complete resource metadata input for the optional local smoke test")
    set(BEACHLIFE_RESOURCE_METADATA_SMOKE_ORACLE "" CACHE FILEPATH
        "Independent expectation sidecar for the optional local smoke test")
    if(BEACHLIFE_RESOURCE_METADATA_SMOKE_INPUT AND BEACHLIFE_RESOURCE_METADATA_SMOKE_ORACLE)
        add_test(NAME reconstructed_resource_metadata_smoke
            COMMAND reconstructed_resource_metadata_smoke
                "${BEACHLIFE_RESOURCE_METADATA_SMOKE_INPUT}"
                "${BEACHLIFE_RESOURCE_METADATA_SMOKE_ORACLE}")
        set_tests_properties(reconstructed_resource_metadata_smoke PROPERTIES
            LABELS "reconstructed;experimental;headless;local-input" TIMEOUT 120)
    endif()
endif()

# Sprite preview inputs and outputs remain caller-supplied local files.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_executable(reconstructed_resource_sprite_preview
        tests/reconstructed_resource_sprite_preview.cpp)
    target_link_libraries(reconstructed_resource_sprite_preview PRIVATE
        beachlife_reconstructed_resource_record_metadata
        beachlife_reconstructed_resource_frame_loaders
        beachlife_reconstructed_resource_raster_direct)
    target_compile_features(reconstructed_resource_sprite_preview PRIVATE cxx_std_20)
    add_dependencies(reconstructed_resource_sprite_preview verify_reconstructed_source)
    set_target_properties(reconstructed_resource_sprite_preview PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(reconstructed_resource_sprite_preview PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(reconstructed_resource_sprite_preview PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_resource_sprite_preview PRIVATE -fno-lto)
    endif()
    set(BEACHLIFE_RESOURCE_SPRITE_PREVIEW_METADATA "" CACHE FILEPATH
        "Resource metadata input for the optional local sprite preview")
    set(BEACHLIFE_RESOURCE_SPRITE_PREVIEW_RAW_BANK "" CACHE FILEPATH
        "Raw frame bank for the optional local sprite preview")
    set(BEACHLIFE_RESOURCE_SPRITE_PREVIEW_ORACLE "" CACHE FILEPATH
        "Independent expectation sidecar for the optional local sprite preview")
    set(BEACHLIFE_RESOURCE_SPRITE_PREVIEW_OUTPUT_PREFIX "" CACHE STRING
        "Optional local prefix for two new verified PPM images")
    if(BEACHLIFE_RESOURCE_SPRITE_PREVIEW_METADATA AND
       BEACHLIFE_RESOURCE_SPRITE_PREVIEW_RAW_BANK AND BEACHLIFE_RESOURCE_SPRITE_PREVIEW_ORACLE)
        set(_resource_sprite_preview_arguments
            "${BEACHLIFE_RESOURCE_SPRITE_PREVIEW_METADATA}"
            "${BEACHLIFE_RESOURCE_SPRITE_PREVIEW_RAW_BANK}"
            "${BEACHLIFE_RESOURCE_SPRITE_PREVIEW_ORACLE}")
        if(BEACHLIFE_RESOURCE_SPRITE_PREVIEW_OUTPUT_PREFIX)
            list(APPEND _resource_sprite_preview_arguments
                "${BEACHLIFE_RESOURCE_SPRITE_PREVIEW_OUTPUT_PREFIX}")
        endif()
        add_test(NAME reconstructed_resource_sprite_preview
            COMMAND reconstructed_resource_sprite_preview ${_resource_sprite_preview_arguments})
        set_tests_properties(reconstructed_resource_sprite_preview PROPERTIES
            LABELS "reconstructed;experimental;headless;local-input" TIMEOUT 120)
        unset(_resource_sprite_preview_arguments)
    endif()
endif()

# Packed encoded sprite raster with independent bounded packet and pixel fixtures.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_resource_raster_packed STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_resource_raster1_packed.cpp")
    target_include_directories(beachlife_reconstructed_resource_raster_packed PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_resource_raster_packed PUBLIC cxx_std_20)
    add_dependencies(beachlife_reconstructed_resource_raster_packed verify_reconstructed_source)

    add_executable(reconstructed_resource_raster_packed_tests
        tests/reconstructed_resource_raster_packed.cpp)
    target_link_libraries(reconstructed_resource_raster_packed_tests PRIVATE
        beachlife_reconstructed_resource_raster_packed)
    set_target_properties(beachlife_reconstructed_resource_raster_packed
        reconstructed_resource_raster_packed_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_resource_raster_packed PRIVATE /GL-)
        target_compile_options(reconstructed_resource_raster_packed_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_resource_raster_packed PRIVATE -fno-lto)
        target_compile_options(reconstructed_resource_raster_packed_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_resource_raster_packed_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_resource_raster_packed
        COMMAND reconstructed_resource_raster_packed_tests)
    set_tests_properties(reconstructed_resource_raster_packed PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Actual active-list traversal and node-only reclamation with generated observers.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_active_list_cleanup STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_active_screen_visit.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_list_clear.cpp")
    target_include_directories(beachlife_reconstructed_active_list_cleanup PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_active_list_cleanup PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_active_list_cleanup PUBLIC
        beachlife_reconstructed_screen_list)
    add_dependencies(beachlife_reconstructed_active_list_cleanup verify_reconstructed_source)

    add_executable(reconstructed_active_list_cleanup_tests
        tests/reconstructed_active_list_cleanup.cpp)
    target_link_libraries(reconstructed_active_list_cleanup_tests PRIVATE
        beachlife_reconstructed_active_list_cleanup)
    set_target_properties(beachlife_reconstructed_active_list_cleanup
        reconstructed_active_list_cleanup_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_active_list_cleanup PRIVATE /GL-)
        target_compile_options(reconstructed_active_list_cleanup_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_active_list_cleanup PRIVATE -fno-lto)
        target_compile_options(reconstructed_active_list_cleanup_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_active_list_cleanup_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_active_list_cleanup
        COMMAND reconstructed_active_list_cleanup_tests)
    set_tests_properties(reconstructed_active_list_cleanup PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Genuine child-ring storage/lifetimes reuse the existing pool implementation.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_screen_child_ring STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/pool_allocate.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_child_ring_constructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_child_ring_clear.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_child_ring_destructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_child_end.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_child_insert.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_child_node.cpp")
    target_include_directories(beachlife_reconstructed_screen_child_ring PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_screen_child_ring PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_screen_child_ring PUBLIC
        beachlife_reconstructed_screen_list)
    add_dependencies(beachlife_reconstructed_screen_child_ring verify_reconstructed_source)

    # Generated Dialog objects now contain a genuinely owning child-ring member.
    # All experimental consumers use the same prepared class declaration.
    target_link_libraries(beachlife_reconstructed_screen_item PUBLIC
        beachlife_reconstructed_screen_child_ring)
    target_link_libraries(beachlife_reconstructed_screen_registry PUBLIC
        beachlife_reconstructed_screen_child_ring)

    add_executable(reconstructed_screen_child_ring_tests
        tests/reconstructed_screen_child_ring.cpp)
    target_link_libraries(reconstructed_screen_child_ring_tests PRIVATE
        beachlife_reconstructed_screen_child_ring
        beachlife_reconstructed_screen_item)
    set_target_properties(beachlife_reconstructed_screen_child_ring
        reconstructed_screen_child_ring_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_screen_child_ring PRIVATE /GL-)
        target_compile_options(reconstructed_screen_child_ring_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_screen_child_ring PRIVATE -fno-lto)
        target_compile_options(reconstructed_screen_child_ring_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_screen_child_ring_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_screen_child_ring
        COMMAND reconstructed_screen_child_ring_tests)
    set_tests_properties(reconstructed_screen_child_ring PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Frame data selection uses the actual resource lifetime and raw file loaders.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_resource_frame_select STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_frame_select.cpp")
    target_include_directories(beachlife_reconstructed_resource_frame_select PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_resource_frame_select PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_resource_frame_select PUBLIC
        beachlife_reconstructed_resource_frame_loaders)
    add_dependencies(beachlife_reconstructed_resource_frame_select verify_reconstructed_source)

    add_executable(reconstructed_resource_frame_select_tests
        tests/reconstructed_resource_frame_select.cpp)
    target_link_libraries(reconstructed_resource_frame_select_tests PRIVATE
        beachlife_reconstructed_resource_frame_select)
    set_target_properties(beachlife_reconstructed_resource_frame_select
        reconstructed_resource_frame_select_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_resource_frame_select PRIVATE /GL-)
        target_compile_options(reconstructed_resource_frame_select_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_resource_frame_select PRIVATE -fno-lto)
        target_compile_options(reconstructed_resource_frame_select_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_resource_frame_select_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_resource_frame_select
        COMMAND reconstructed_resource_frame_select_tests
            "${CMAKE_CURRENT_BINARY_DIR}/generated/resource-frame-select")
    set_tests_properties(reconstructed_resource_frame_select PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Standalone refill reuses the existing chunk implementation and sole globals.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_pool_refill STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/pool_refill.cpp")
    target_include_directories(beachlife_reconstructed_pool_refill PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_pool_refill PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_pool_refill PUBLIC
        beachlife_reconstructed_screen_list)
    add_dependencies(beachlife_reconstructed_pool_refill verify_reconstructed_source)

    add_executable(reconstructed_pool_refill_tests tests/reconstructed_pool_refill.cpp)
    target_link_libraries(reconstructed_pool_refill_tests PRIVATE
        beachlife_reconstructed_pool_refill)
    set_target_properties(beachlife_reconstructed_pool_refill reconstructed_pool_refill_tests
        PROPERTIES CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_pool_refill PRIVATE /GL-)
        target_compile_options(reconstructed_pool_refill_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_pool_refill PRIVATE -fno-lto)
        target_compile_options(reconstructed_pool_refill_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_pool_refill_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_pool_refill COMMAND reconstructed_pool_refill_tests)
    set_tests_properties(reconstructed_pool_refill PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Actual active-node erasure reuses the existing typed list and pool owners.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_active_node_erase STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_list_erase.cpp")
    target_include_directories(beachlife_reconstructed_active_node_erase PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_active_node_erase PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_active_node_erase PUBLIC
        beachlife_reconstructed_active_list_cleanup)
    add_dependencies(beachlife_reconstructed_active_node_erase verify_reconstructed_source)

    add_executable(reconstructed_active_node_erase_tests
        tests/reconstructed_active_node_erase.cpp)
    target_link_libraries(reconstructed_active_node_erase_tests PRIVATE
        beachlife_reconstructed_active_node_erase)
    set_target_properties(beachlife_reconstructed_active_node_erase
        reconstructed_active_node_erase_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_active_node_erase PRIVATE /GL-)
        target_compile_options(reconstructed_active_node_erase_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_active_node_erase PRIVATE -fno-lto)
        target_compile_options(reconstructed_active_node_erase_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_active_node_erase_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_active_node_erase COMMAND reconstructed_active_node_erase_tests)
    set_tests_properties(reconstructed_active_node_erase PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# The matched one-word setter uses the genuine experimental renderer aggregate.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_renderer_word60 STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_set_word60.cpp")
    target_include_directories(beachlife_reconstructed_renderer_word60 PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_renderer_word60 PUBLIC cxx_std_20)
    add_dependencies(beachlife_reconstructed_renderer_word60 verify_reconstructed_source)

    add_executable(reconstructed_renderer_word60_tests tests/reconstructed_renderer_word60.cpp)
    target_link_libraries(reconstructed_renderer_word60_tests PRIVATE
        beachlife_reconstructed_renderer_word60)
    set_target_properties(beachlife_reconstructed_renderer_word60
        reconstructed_renderer_word60_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_renderer_word60 PRIVATE /GL-)
        target_compile_options(reconstructed_renderer_word60_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_renderer_word60 PRIVATE -fno-lto)
        target_compile_options(reconstructed_renderer_word60_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_renderer_word60_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_renderer_word60 COMMAND reconstructed_renderer_word60_tests)
    set_tests_properties(reconstructed_renderer_word60 PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_executable(reconstructed_resource_raster2_tests
        tests/reconstructed_resource_raster2.cpp)
    target_link_libraries(reconstructed_resource_raster2_tests PRIVATE
        beachlife_reconstructed_resource_raster2)
    set_target_properties(reconstructed_resource_raster2_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(reconstructed_resource_raster2_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(reconstructed_resource_raster2_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_resource_raster2_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_resource_raster2 COMMAND reconstructed_resource_raster2_tests)
    set_tests_properties(reconstructed_resource_raster2 PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# The Intro TGA command composes real readers, widget and registration lifetimes.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_gui_tga_command STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_parse_tga.cpp")
    target_include_directories(beachlife_reconstructed_gui_tga_command PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_gui_tga_command PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_gui_tga_command PUBLIC
        beachlife_reconstructed_gui_context
        beachlife_reconstructed_gui_rectangle
        beachlife_reconstructed_tga_widget)
    add_dependencies(beachlife_reconstructed_gui_tga_command verify_reconstructed_source)
    add_executable(reconstructed_gui_tga_command_tests
        tests/reconstructed_gui_tga_command.cpp)
    target_link_libraries(reconstructed_gui_tga_command_tests PRIVATE
        beachlife_reconstructed_gui_tga_command)
    set_target_properties(beachlife_reconstructed_gui_tga_command
        reconstructed_gui_tga_command_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_gui_tga_command PRIVATE /GL-)
        target_compile_options(reconstructed_gui_tga_command_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_gui_tga_command PRIVATE -fno-lto)
        target_compile_options(reconstructed_gui_tga_command_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_gui_tga_command_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_gui_tga_command COMMAND reconstructed_gui_tga_command_tests)
    add_test(NAME reconstructed_gui_tga_command_fatal
        COMMAND reconstructed_gui_tga_command_tests --fatal)
    set_tests_properties(reconstructed_gui_tga_command reconstructed_gui_tga_command_fatal
        PROPERTIES LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_executable(reconstructed_render_mask_setup_tests
        tests/reconstructed_render_mask_setup.cpp)
    target_link_libraries(reconstructed_render_mask_setup_tests PRIVATE
        beachlife_reconstructed_render_masks)
    set_target_properties(reconstructed_render_mask_setup_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(reconstructed_render_mask_setup_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(reconstructed_render_mask_setup_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_render_mask_setup_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_render_mask_setup COMMAND reconstructed_render_mask_setup_tests)
    set_tests_properties(reconstructed_render_mask_setup PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_executable(reconstructed_renderer_present_effect_tests
        tests/reconstructed_renderer_present_effect.cpp)
    target_link_libraries(reconstructed_renderer_present_effect_tests PRIVATE
        beachlife_reconstructed_renderer_present_effect)
    set_target_properties(reconstructed_renderer_present_effect_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(reconstructed_renderer_present_effect_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(reconstructed_renderer_present_effect_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_renderer_present_effect_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_renderer_present_effect
        COMMAND reconstructed_renderer_present_effect_tests)
    set_tests_properties(reconstructed_renderer_present_effect PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_executable(reconstructed_renderer_default_tests
        tests/reconstructed_renderer_default.cpp)
    target_link_libraries(reconstructed_renderer_default_tests PRIVATE
        beachlife_reconstructed_renderer_default)
    set_target_properties(reconstructed_renderer_default_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(reconstructed_renderer_default_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(reconstructed_renderer_default_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_renderer_default_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_renderer_default COMMAND reconstructed_renderer_default_tests)
    set_tests_properties(reconstructed_renderer_default PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# The position setter shares the real base-item lifetime and virtual-method graph.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_screen_item_rectangle_position STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_rectangle_position.cpp")
    target_include_directories(beachlife_reconstructed_screen_item_rectangle_position PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_screen_item_rectangle_position PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_screen_item_rectangle_position PUBLIC
        beachlife_reconstructed_screen_item)
    add_dependencies(beachlife_reconstructed_screen_item_rectangle_position verify_reconstructed_source)

    add_executable(reconstructed_screen_item_rectangle_position_tests
        tests/reconstructed_screen_item_rectangle_position.cpp)
    target_link_libraries(reconstructed_screen_item_rectangle_position_tests PRIVATE
        beachlife_reconstructed_screen_item_rectangle_position)
    set_target_properties(beachlife_reconstructed_screen_item_rectangle_position
        reconstructed_screen_item_rectangle_position_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_screen_item_rectangle_position PRIVATE /GL-)
        target_compile_options(reconstructed_screen_item_rectangle_position_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_screen_item_rectangle_position PRIVATE -fno-lto)
        target_compile_options(reconstructed_screen_item_rectangle_position_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_screen_item_rectangle_position_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_screen_item_rectangle_position
        COMMAND reconstructed_screen_item_rectangle_position_tests)
    set_tests_properties(reconstructed_screen_item_rectangle_position PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

if(BUILD_TESTING AND BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_executable(reconstructed_resource_raster_402880_tests
        tests/reconstructed_resource_raster_402880.cpp)
    target_link_libraries(reconstructed_resource_raster_402880_tests PRIVATE
        beachlife_reconstructed_resource_raster_402880)
    set_target_properties(reconstructed_resource_raster_402880_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(reconstructed_resource_raster_402880_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(reconstructed_resource_raster_402880_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_resource_raster_402880_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_resource_raster_402880
        COMMAND reconstructed_resource_raster_402880_tests)
    set_tests_properties(reconstructed_resource_raster_402880 PROPERTIES
        LABELS "reconstructed;headless;experimental" TIMEOUT 30)
endif()

# The complete defined-item class reuses the real base and owning child ring.
# This optional subsystem target retains the qualified test-only pool boundary.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_gui_defined_item STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_defined_item_constructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_defined_item_globals.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_defined_item_destructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_defined_item_virtuals.cpp")
    target_include_directories(beachlife_reconstructed_gui_defined_item PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_gui_defined_item PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_gui_defined_item PUBLIC
        beachlife_reconstructed_screen_item
        beachlife_reconstructed_screen_child_ring)
    add_dependencies(beachlife_reconstructed_gui_defined_item verify_reconstructed_source)

    add_executable(reconstructed_gui_defined_item_tests
        tests/reconstructed_gui_defined_item.cpp)
    target_link_libraries(reconstructed_gui_defined_item_tests PRIVATE
        beachlife_reconstructed_gui_defined_item)
    set_target_properties(beachlife_reconstructed_gui_defined_item
        reconstructed_gui_defined_item_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_gui_defined_item PRIVATE /GL-)
        target_compile_options(reconstructed_gui_defined_item_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_gui_defined_item PRIVATE -fno-lto)
        target_compile_options(reconstructed_gui_defined_item_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_gui_defined_item_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_gui_defined_item COMMAND reconstructed_gui_defined_item_tests)
    set_tests_properties(reconstructed_gui_defined_item PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()


if(BUILD_TESTING AND BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_executable(reconstructed_resource_raster_405b20_tests
        tests/reconstructed_resource_raster_405b20.cpp)
    target_link_libraries(reconstructed_resource_raster_405b20_tests PRIVATE
        beachlife_reconstructed_resource_raster_405b20)
    set_target_properties(reconstructed_resource_raster_405b20_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(reconstructed_resource_raster_405b20_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(reconstructed_resource_raster_405b20_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_resource_raster_405b20_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_resource_raster_405b20
        COMMAND reconstructed_resource_raster_405b20_tests)
    set_tests_properties(reconstructed_resource_raster_405b20 PROPERTIES
        LABELS "reconstructed;headless;experimental" TIMEOUT 30)
endif()


# Complete image conversions use the genuine existing image allocation/cleanup.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_tiff_conversions STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/tiff_image_conversions.cpp")
    target_include_directories(beachlife_reconstructed_tiff_conversions PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_tiff_conversions PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_tiff_conversions PUBLIC
        beachlife_reconstructed_tga)
    add_dependencies(beachlife_reconstructed_tiff_conversions verify_reconstructed_source)
    add_executable(reconstructed_tiff_image_conversions_tests
        tests/reconstructed_tiff_image_conversions.cpp)
    target_link_libraries(reconstructed_tiff_image_conversions_tests PRIVATE
        beachlife_reconstructed_tiff_conversions)
    set_target_properties(beachlife_reconstructed_tiff_conversions
        reconstructed_tiff_image_conversions_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_tiff_conversions PRIVATE /GL-)
        target_compile_options(reconstructed_tiff_image_conversions_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_tiff_conversions PRIVATE -fno-lto)
        target_compile_options(reconstructed_tiff_image_conversions_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_tiff_image_conversions_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_tiff_image_conversions
        COMMAND reconstructed_tiff_image_conversions_tests)
    set_tests_properties(reconstructed_tiff_image_conversions PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Complete experimental TIFF loading uses real image owners and the mounted file session.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_tiff_loader STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/tiff_image_loader.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/tiff_directory_reader.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/tiff_decode_strips.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/tiff_decode_tiles.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/tiff_decode_packbits.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/tiff_read_palette.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/tiff_palette_byte.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/tiff_vfs_read_word.cpp")
    target_include_directories(beachlife_reconstructed_tiff_loader PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_tiff_loader PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_tiff_loader PUBLIC
        beachlife_reconstructed_tiff_conversions
        beachlife_retail_files)
    add_dependencies(beachlife_reconstructed_tiff_loader verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_tiff_loader PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)

    add_executable(reconstructed_tiff_image_loader_tests
        tests/reconstructed_tiff_image_loader.cpp)
    target_link_libraries(reconstructed_tiff_image_loader_tests PRIVATE
        beachlife_reconstructed_tiff_loader)
    set_target_properties(reconstructed_tiff_image_loader_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_tiff_loader PRIVATE /GL-)
        target_compile_options(reconstructed_tiff_image_loader_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_tiff_loader PRIVATE -fno-lto)
        target_compile_options(reconstructed_tiff_image_loader_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_tiff_image_loader_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_tiff_image_loader
        COMMAND reconstructed_tiff_image_loader_tests
            "${CMAKE_CURRENT_BINARY_DIR}/generated/tiff-loader")
    set_tests_properties(reconstructed_tiff_image_loader PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Genuine font data owns its TIFF image and reads both metric files through the VFS.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_font_data STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_font_data_constructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_font_data_destructor.cpp")
    target_include_directories(beachlife_reconstructed_font_data PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_font_data PUBLIC cxx_std_20)
    # Existing GUI rectangle archive supplies the real fatal routine and renderer global.
    target_link_libraries(beachlife_reconstructed_font_data PUBLIC
        beachlife_reconstructed_files
        beachlife_reconstructed_tiff_loader
        beachlife_reconstructed_gui_rectangle)
    add_dependencies(beachlife_reconstructed_font_data verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_font_data PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)

    # The three existing metric consumers now use the one genuine owning declaration.
    target_link_libraries(beachlife_reconstructed_font_measurement PUBLIC
        beachlife_reconstructed_font_data)
    add_executable(reconstructed_font_data_owner_tests
        tests/reconstructed_font_data_owner.cpp)
    target_link_libraries(reconstructed_font_data_owner_tests PRIVATE
        beachlife_reconstructed_font_data beachlife_reconstructed_font_measurement)
    set_target_properties(reconstructed_font_data_owner_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_font_data PRIVATE /GL-)
        target_compile_options(reconstructed_font_data_owner_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_font_data PRIVATE -fno-lto)
        target_compile_options(reconstructed_font_data_owner_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_font_data_owner_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_font_data_owner
        COMMAND reconstructed_font_data_owner_tests
            "${CMAKE_CURRENT_BINARY_DIR}/generated/font-data-owner")
    set_tests_properties(reconstructed_font_data_owner PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Real font glyph writers reuse the existing constructor, colour and global owners.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_font_glyphs STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_font_glyph_direct.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_font_glyph_surface.cpp")
    target_include_directories(beachlife_reconstructed_font_glyphs PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_font_glyphs PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_font_glyphs PUBLIC
        beachlife_reconstructed_font_measurement
        beachlife_reconstructed_frame_fill_styles)
    add_dependencies(beachlife_reconstructed_font_glyphs verify_reconstructed_source)

    add_executable(reconstructed_font_glyphs_tests tests/reconstructed_font_glyphs.cpp)
    target_link_libraries(reconstructed_font_glyphs_tests PRIVATE
        beachlife_reconstructed_font_glyphs)
    set_target_properties(beachlife_reconstructed_font_glyphs
        reconstructed_font_glyphs_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_font_glyphs PRIVATE /GL-)
        target_compile_options(reconstructed_font_glyphs_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_font_glyphs PRIVATE -fno-lto)
        target_compile_options(reconstructed_font_glyphs_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_font_glyphs_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_font_glyphs COMMAND reconstructed_font_glyphs_tests)
    set_tests_properties(reconstructed_font_glyphs PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Complete text layout and glyph drawing use the real file-loaded font owner.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_font_drawing STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_font_draw_text.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_font_draw_line.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_font_draw_settings.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_surface_unlock.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_surface_pitch.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_surface_position.cpp")
    target_include_directories(beachlife_reconstructed_font_drawing PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_font_drawing PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_font_drawing PUBLIC
        beachlife_reconstructed_font_glyphs
        beachlife_reconstructed_font_data
        beachlife_reconstructed_surface_pixels)
    add_dependencies(beachlife_reconstructed_font_drawing verify_reconstructed_source)

    add_executable(reconstructed_font_drawing_tests tests/reconstructed_font_drawing.cpp)
    target_link_libraries(reconstructed_font_drawing_tests PRIVATE
        beachlife_reconstructed_font_drawing)
    set_target_properties(beachlife_reconstructed_font_drawing
        reconstructed_font_drawing_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_font_drawing PRIVATE /GL-)
        target_compile_options(reconstructed_font_drawing_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_font_drawing PRIVATE -fno-lto)
        target_compile_options(reconstructed_font_drawing_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_font_drawing_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_font_drawing COMMAND reconstructed_font_drawing_tests
        "${CMAKE_CURRENT_BINARY_DIR}/generated/font-drawing")
    set_tests_properties(reconstructed_font_drawing PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_executable(resource_files_tests tests/resource_files.cpp)
    target_link_libraries(resource_files_tests PRIVATE
        beachlife_resource_files beachlife_retail_files)
    target_compile_features(resource_files_tests PRIVATE cxx_std_20)
    set_target_properties(resource_files_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(resource_files_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(resource_files_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(resource_files_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME resource_files
        COMMAND resource_files_tests "${CMAKE_CURRENT_BINARY_DIR}/generated/resource-files")
    set_tests_properties(resource_files PROPERTIES
        LABELS "platform;experimental;headless" TIMEOUT 30)
endif()

# The genuine Text table uses the complete base and file-loaded font drawing.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_gui_text_item STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_text_item_constructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_text_item_destructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_text_item_draw.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_text_item_activation.cpp")
    target_include_directories(beachlife_reconstructed_gui_text_item PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_gui_text_item PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_gui_text_item PUBLIC
        beachlife_reconstructed_screen_item_complete
        beachlife_reconstructed_font_drawing)
    add_dependencies(beachlife_reconstructed_gui_text_item verify_reconstructed_source)

    add_executable(reconstructed_gui_text_item_tests tests/reconstructed_gui_text_item.cpp)
    target_link_libraries(reconstructed_gui_text_item_tests PRIVATE
        beachlife_reconstructed_gui_text_item)
    set_target_properties(beachlife_reconstructed_gui_text_item
        reconstructed_gui_text_item_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_gui_text_item PRIVATE /GL-)
        target_compile_options(reconstructed_gui_text_item_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_gui_text_item PRIVATE -fno-lto)
        target_compile_options(reconstructed_gui_text_item_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_gui_text_item_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_gui_text_item COMMAND reconstructed_gui_text_item_tests
        "${CMAKE_CURRENT_BINARY_DIR}/generated/gui-text-item")
    set_tests_properties(reconstructed_gui_text_item PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Script TEXT construction selects the real readers, registration and Text owner.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_gui_parse_text STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_parse_text.cpp")
    target_include_directories(beachlife_reconstructed_gui_parse_text PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_gui_parse_text PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_gui_parse_text PUBLIC
        beachlife_reconstructed_gui_text_item
        beachlife_reconstructed_gui_context
        beachlife_reconstructed_gui_rectangle)
    add_dependencies(beachlife_reconstructed_gui_parse_text verify_reconstructed_source)

    add_executable(reconstructed_gui_parse_text_tests tests/reconstructed_gui_parse_text.cpp)
    target_link_libraries(reconstructed_gui_parse_text_tests PRIVATE
        beachlife_reconstructed_gui_parse_text)
    set_target_properties(beachlife_reconstructed_gui_parse_text
        reconstructed_gui_parse_text_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_gui_parse_text PRIVATE /GL-)
        target_compile_options(reconstructed_gui_parse_text_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(beachlife_reconstructed_gui_parse_text PRIVATE -fno-lto)
        target_compile_options(reconstructed_gui_parse_text_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_gui_parse_text_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_gui_parse_text COMMAND reconstructed_gui_parse_text_tests)
    add_test(NAME reconstructed_gui_parse_text_fatal_open
        COMMAND reconstructed_gui_parse_text_tests --fatal-open)
    set_tests_properties(reconstructed_gui_parse_text reconstructed_gui_parse_text_fatal_open
        PROPERTIES LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Complete resource owner and mounted metadata/palette/raw bank composition.
# All these subsystems already live below the optional-test return.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_resource_pool_host STATIC src/runtime/resource_pool_host.cpp)
    target_compile_features(beachlife_resource_pool_host PUBLIC cxx_std_20)
    target_link_libraries(beachlife_resource_pool_host PUBLIC Threads::Threads)
    # Pool lock calls in the actual list archive use the real host boundary.
    target_link_libraries(beachlife_reconstructed_screen_list PUBLIC
        $<LINK_ONLY:beachlife_resource_pool_host>)

    add_library(beachlife_reconstructed_resource_stream_read STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_resource_stream_read.cpp")
    target_include_directories(beachlife_reconstructed_resource_stream_read PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_resource_stream_read PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_resource_stream_read PUBLIC
        beachlife_resource_files)
    add_dependencies(beachlife_reconstructed_resource_stream_read verify_reconstructed_source)

    # Preserve the original FILE* bodies and their stdio consumers. These new
    # overloads are native ports of the same methods, not additional originals.
    target_sources(beachlife_reconstructed_resource_frame_metadata PRIVATE
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_frame_vfs_metadata.cpp")
    target_link_libraries(beachlife_reconstructed_resource_frame_metadata PUBLIC
        beachlife_reconstructed_resource_stream_read)
    target_sources(beachlife_reconstructed_resource_record_metadata PRIVATE
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_record_vfs_metadata.cpp")
    target_link_libraries(beachlife_reconstructed_resource_record_metadata PUBLIC
        beachlife_reconstructed_resource_stream_read)
    target_link_libraries(beachlife_reconstructed_resource_file_producer PUBLIC
        beachlife_resource_files)
    target_link_libraries(beachlife_reconstructed_resource_frame_loaders PUBLIC
        beachlife_resource_files)
    target_link_libraries(beachlife_reconstructed_resource_palette_modifiers PUBLIC
        beachlife_resource_files)

    # Move the three already-qualified wrapper sources from their fixture into
    # the real shared archive. No second body/global or comparison anchor.
    target_sources(beachlife_reconstructed_resource_find PRIVATE
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_resource_lookup.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_resource_constructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_resource_destructor.cpp")
    target_link_libraries(beachlife_reconstructed_resource_find PUBLIC
        beachlife_resource_files)
    set_property(TARGET reconstructed_resource_find_tests PROPERTY SOURCES
        "${CMAKE_SOURCE_DIR}/tests/reconstructed_resource_find.cpp")

    add_library(beachlife_reconstructed_resource_owner STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_resource_manager_constructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_resource_manager_destructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_resource_registration.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_resource_ring_constructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_resource_ring_clear.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_resource_ring_destructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_resource_initial_name.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_resource_release.cpp")
    target_include_directories(beachlife_reconstructed_resource_owner PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_resource_owner PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_resource_owner PUBLIC
        beachlife_reconstructed_resource_file_producer
        beachlife_reconstructed_resource_frame_select
        beachlife_reconstructed_resource_palette_modifiers
        beachlife_reconstructed_resource_find
        beachlife_reconstructed_screen_child_ring
        beachlife_resource_pool_host SDL3::SDL3)
    add_dependencies(beachlife_reconstructed_resource_owner verify_reconstructed_source)

    # These links are necessary only with the genuine-construction migrations.
    # Each migrated fixture constructs and destroys the genuine owner.
    target_link_libraries(reconstructed_resource_find_tests PRIVATE
        beachlife_reconstructed_resource_owner)
    if(TARGET reconstructed_resource_file_producer_tests)
        target_link_libraries(reconstructed_resource_file_producer_tests PRIVATE
            beachlife_reconstructed_resource_owner)
    endif()

    add_executable(reconstructed_resource_owner_tests tests/reconstructed_resource_owner.cpp)
    target_link_libraries(reconstructed_resource_owner_tests PRIVATE
        beachlife_reconstructed_resource_owner)
    foreach(target IN ITEMS beachlife_resource_pool_host
        beachlife_reconstructed_resource_stream_read beachlife_reconstructed_resource_owner
        reconstructed_resource_owner_tests)
        set_target_properties(${target} PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        if(MSVC)
            target_compile_options(${target} PRIVATE /GL-)
        else()
            target_compile_options(${target} PRIVATE -fno-lto)
        endif()
    endforeach()
    if(MSVC)
        target_compile_options(reconstructed_resource_owner_tests PRIVATE /W4 /WX)
    else()
        target_compile_options(reconstructed_resource_owner_tests PRIVATE
            -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_resource_owner_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_resource_owner
        COMMAND reconstructed_resource_owner_tests
            "${CMAKE_CURRENT_BINARY_DIR}/generated/resource-owner")
    set_tests_properties(reconstructed_resource_owner PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Genuine FONT parsing publishes real file-loaded font owners into typed slots.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_gui_parse_font STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_parse_font.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_font_publication.cpp")
    target_include_directories(beachlife_reconstructed_gui_parse_font PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_gui_parse_font PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_gui_parse_font PUBLIC
        beachlife_reconstructed_gui_integer
        beachlife_reconstructed_font_measurement
        beachlife_reconstructed_screen_localization)
    add_dependencies(beachlife_reconstructed_gui_parse_font verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_gui_parse_font PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_gui_parse_font PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_gui_parse_font PRIVATE -fno-lto)
    endif()

    # A single test-only GNU forwarding wrapper observes/fails the actual later
    # scalar allocation. Successful allocations still use the real C++ runtime.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT MSVC)
        add_executable(reconstructed_gui_parse_font_tests tests/reconstructed_gui_parse_font.cpp)
        target_link_libraries(reconstructed_gui_parse_font_tests PRIVATE
            beachlife_reconstructed_gui_parse_font)
        set_target_properties(reconstructed_gui_parse_font_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_gui_parse_font_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_gui_parse_font_tests PRIVATE
            -fno-lto -Wl,--wrap=_Znwm)
        add_test(NAME reconstructed_gui_parse_font
            COMMAND reconstructed_gui_parse_font_tests
                "${CMAKE_CURRENT_BINARY_DIR}/generated/gui-parse-font")
        set_tests_properties(reconstructed_gui_parse_font PROPERTIES
            LABELS "reconstructed;experimental;headless" TIMEOUT 30)
    endif()
endif()

# Complete FrameItem owns a real child ring and draws through real resources.
# Its base/resource dependencies already belong to the optional subsystem graph.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_frame_item STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_resource_raster12.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_resource_raster12_mode2.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_resource_raster3.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_resource_raster5_mode2.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_resource_raster6.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_resource_raster_401df0.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_resource_raster_402350.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_resource_raster_403f60.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_resource_raster_404750.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_resource_raster_405180.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_resource_raster_4087b0.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_resource_raster_flagged.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_resource_dispatch.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_resource_draw.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_resource_timing.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_frame_activation.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_frame_attach.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_frame_callback.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_frame_constructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_frame_coordinates.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_frame_destructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_frame_draw.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_frame_elapsed.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_frame_event.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_frame_hit.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_frame_prepare.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_frame_style1.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_frame_style4.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_frame_style5.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_frame_style_initialize.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_frame_style_name_owners.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_frame_style_native_storage.cpp")
    target_include_directories(beachlife_reconstructed_frame_item PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_frame_item PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_frame_item PUBLIC
        beachlife_reconstructed_screen_item_complete
        beachlife_reconstructed_screen_child_ring
        beachlife_reconstructed_resource_owner
        beachlife_reconstructed_renderer_line
        beachlife_reconstructed_renderer_clip
        beachlife_reconstructed_font_drawing
        beachlife_reconstructed_resource_raster_direct
        beachlife_reconstructed_resource_raster_packed
        beachlife_reconstructed_resource_raster2
        beachlife_reconstructed_resource_raster_402880
        beachlife_reconstructed_resource_raster_405b20
        beachlife_reconstructed_render_masks)
    # Auxiliary positioning in the complete item archive uses real Frame RTTI.
    # Link the actual static dependency without changing compile usage.
    target_link_libraries(beachlife_reconstructed_screen_item_complete PUBLIC
        $<LINK_ONLY:beachlife_reconstructed_frame_item>)
    add_dependencies(beachlife_reconstructed_frame_item verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_frame_item PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_frame_item PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_frame_item PRIVATE -fno-lto)
    endif()

    # The finite generated ownership fixture uses the qualified 64-bit pool
    # backing. It supplies no game method, allocator, vtable or COM provider.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(reconstructed_frame_item_tests tests/reconstructed_frame_item.cpp)
        target_link_libraries(reconstructed_frame_item_tests PRIVATE
            beachlife_reconstructed_frame_item)
        set_target_properties(reconstructed_frame_item_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_frame_item_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_frame_item_tests PRIVATE -fno-lto)
        add_test(NAME reconstructed_frame_item COMMAND reconstructed_frame_item_tests
            "${CMAKE_CURRENT_BINARY_DIR}/generated/frame-item-owner")
        set_tests_properties(reconstructed_frame_item PROPERTIES
            LABELS "reconstructed;experimental;headless" TIMEOUT 30)
    endif()
endif()
# Complete MAIN handling creates the genuine Frame and uses actual context links.
# Frame/resource/font dependencies retain their existing optional subsystem scope.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_gui_parse_main STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_parse_main_item.cpp")
    target_include_directories(beachlife_reconstructed_gui_parse_main PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_gui_parse_main PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_gui_parse_main PUBLIC
        beachlife_reconstructed_frame_item
        beachlife_reconstructed_gui_context
        beachlife_reconstructed_gui_rectangle)
    add_dependencies(beachlife_reconstructed_gui_parse_main verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_gui_parse_main PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_gui_parse_main PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_gui_parse_main PRIVATE -fno-lto)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(reconstructed_gui_parse_main_tests tests/reconstructed_gui_parse_main_item.cpp)
        target_link_libraries(reconstructed_gui_parse_main_tests PRIVATE
            beachlife_reconstructed_gui_parse_main)
        set_target_properties(reconstructed_gui_parse_main_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_gui_parse_main_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_gui_parse_main_tests PRIVATE -fno-lto)
        add_test(NAME reconstructed_gui_parse_main COMMAND reconstructed_gui_parse_main_tests)
        add_test(NAME reconstructed_gui_parse_main_fatal_open
            COMMAND reconstructed_gui_parse_main_tests --fatal-open)
        set_tests_properties(reconstructed_gui_parse_main reconstructed_gui_parse_main_fatal_open
            PROPERTIES LABELS "reconstructed;experimental;headless" TIMEOUT 30)
    endif()
endif()
# The complete Dialog table owns real names, Frames and child rings.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_dialog_item STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_position.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_prepare.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_name_search.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_activation.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_attach.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_begin_transition.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_constructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_coordinates.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_destructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_draw.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_elapsed.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_hit.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_input38.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_move.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_names.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_resource_hit.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_slot08.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_slot1c.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_dialog_word4c.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_manager_close.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_manager_prepare_named.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_surface_prepare.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/window_activation_globals.cpp")
    target_include_directories(beachlife_reconstructed_dialog_item PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_dialog_item PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_dialog_item PUBLIC
        beachlife_reconstructed_frame_item
        beachlife_reconstructed_screen_placements)
    add_dependencies(beachlife_reconstructed_dialog_item verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_dialog_item PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_dialog_item PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_dialog_item PRIVATE -fno-lto)
    endif()

    # The shared experimental Dialog declaration has one genuine constructor.
    # These existing fixture archives retain their real methods; their generated
    # subtypes now reach the actual owned name/Frame/rings instead of inheritance.
    target_link_libraries(beachlife_reconstructed_screen_item PUBLIC
        beachlife_reconstructed_dialog_item)
    target_link_libraries(beachlife_reconstructed_screen_registry PUBLIC
        beachlife_reconstructed_dialog_item)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(reconstructed_dialog_item_tests tests/reconstructed_dialog_item.cpp)
        target_link_libraries(reconstructed_dialog_item_tests PRIVATE
            beachlife_reconstructed_dialog_item)
        set_target_properties(reconstructed_dialog_item_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_dialog_item_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_dialog_item_tests PRIVATE -fno-lto)
        add_test(NAME reconstructed_dialog_item COMMAND reconstructed_dialog_item_tests
            "${CMAKE_CURRENT_BINARY_DIR}/generated/dialog-item-owner")
        set_tests_properties(reconstructed_dialog_item PROPERTIES
            LABELS "reconstructed;experimental;headless" TIMEOUT 30)
    endif()
endif()
# Complete software-surface allocation/table growth using existing real pool/cleanup.
# This subsystem target remains below the existing reconstructed-test return.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_surface_create STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_surface_create.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_surface_initialize.cpp")
    target_include_directories(beachlife_reconstructed_surface_create PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_surface_create PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_surface_create PUBLIC
        beachlife_reconstructed_pool_refill
        beachlife_reconstructed_surfaces
        beachlife_reconstructed_gui_rectangle)
    add_dependencies(beachlife_reconstructed_surface_create verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_surface_create PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_surface_create PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_surface_create PRIVATE -fno-lto)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(reconstructed_surface_create_tests tests/reconstructed_surface_create.cpp)
        target_link_libraries(reconstructed_surface_create_tests PRIVATE
            beachlife_reconstructed_surface_create)
        set_target_properties(reconstructed_surface_create_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_surface_create_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_surface_create_tests PRIVATE -fno-lto)
        add_test(NAME reconstructed_surface_create COMMAND reconstructed_surface_create_tests)
        set_tests_properties(reconstructed_surface_create PROPERTIES
            LABELS "reconstructed;experimental;headless" TIMEOUT 30)
    endif()
endif()
# The complete Dialog handler uses the real owned Dialog/Frame and parser links.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_gui_parse_dialog STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_parse_dialog.cpp")
    target_include_directories(beachlife_reconstructed_gui_parse_dialog PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_gui_parse_dialog PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_gui_parse_dialog PUBLIC
        beachlife_reconstructed_dialog_item
        beachlife_reconstructed_gui_context
        beachlife_reconstructed_gui_rectangle)
    add_dependencies(beachlife_reconstructed_gui_parse_dialog verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_gui_parse_dialog PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_gui_parse_dialog PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_gui_parse_dialog PRIVATE -fno-lto)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(reconstructed_gui_parse_dialog_tests tests/reconstructed_gui_parse_dialog.cpp)
        target_link_libraries(reconstructed_gui_parse_dialog_tests PRIVATE
            beachlife_reconstructed_gui_parse_dialog)
        set_target_properties(reconstructed_gui_parse_dialog_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_gui_parse_dialog_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_gui_parse_dialog_tests PRIVATE -fno-lto)
        add_test(NAME reconstructed_gui_parse_dialog COMMAND reconstructed_gui_parse_dialog_tests)
        add_test(NAME reconstructed_gui_parse_dialog_fatal_bracket
            COMMAND reconstructed_gui_parse_dialog_tests --fatal-bracket)
        add_test(NAME reconstructed_gui_parse_dialog_fatal_brace
            COMMAND reconstructed_gui_parse_dialog_tests --fatal-brace)
        set_tests_properties(reconstructed_gui_parse_dialog
            reconstructed_gui_parse_dialog_fatal_bracket reconstructed_gui_parse_dialog_fatal_brace
            PROPERTIES LABELS "reconstructed;experimental;headless" TIMEOUT 30)
        # Keep the real process-exit gate. Qualification also checks the two
        # exact cleanup markers; a matching line must not override a failed exit.
    endif()
endif()
# Complete RequestScreen keeps real Dialog, list and software-surface children.
# This optional subsystem target remains below the existing test-only return.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_screen_request STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_request.cpp")
    target_include_directories(beachlife_reconstructed_screen_request PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_screen_request PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_screen_request PUBLIC
        beachlife_reconstructed_dialog_item
        beachlife_reconstructed_surface_create
        beachlife_reconstructed_active_list_cleanup
        beachlife_reconstructed_gui_context)
    add_dependencies(beachlife_reconstructed_screen_request verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_screen_request PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_screen_request PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_screen_request PRIVATE -fno-lto)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(reconstructed_screen_request_tests tests/reconstructed_screen_request.cpp)
        target_link_libraries(reconstructed_screen_request_tests PRIVATE
            beachlife_reconstructed_screen_request)
        set_target_properties(reconstructed_screen_request_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_screen_request_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_screen_request_tests PRIVATE -fno-lto)
        add_test(NAME reconstructed_screen_request COMMAND reconstructed_screen_request_tests)
        set_tests_properties(reconstructed_screen_request PROPERTIES
            LABELS "reconstructed;experimental;headless" TIMEOUT 30)
    endif()
endif()
# Closing active screens keeps real Dialog, list, placement and surface owners.
# This focused optional subsystem follows the existing test-only return.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_close_active_screens STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_close_active.cpp")
    target_include_directories(beachlife_reconstructed_close_active_screens PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_close_active_screens PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_close_active_screens PUBLIC
        beachlife_reconstructed_screen_request
        beachlife_reconstructed_active_node_erase)
    add_dependencies(beachlife_reconstructed_close_active_screens verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_close_active_screens PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_close_active_screens PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_close_active_screens PRIVATE -fno-lto)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(reconstructed_close_active_screens_tests
            tests/reconstructed_close_active_screens.cpp)
        target_link_libraries(reconstructed_close_active_screens_tests PRIVATE
            beachlife_reconstructed_close_active_screens)
        set_target_properties(reconstructed_close_active_screens_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_close_active_screens_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_close_active_screens_tests PRIVATE -fno-lto)
        add_test(NAME reconstructed_close_active_screens
            COMMAND reconstructed_close_active_screens_tests)
        set_tests_properties(reconstructed_close_active_screens PROPERTIES
            LABELS "reconstructed;experimental;headless" TIMEOUT 30)
    endif()
endif()
# The complete Define handler uses the real defined-item, ring and parser links.
# This optional subsystem remains below the existing reconstructed-test return.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_gui_parse_define STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_parse_define.cpp")
    target_include_directories(beachlife_reconstructed_gui_parse_define PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_gui_parse_define PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_gui_parse_define PUBLIC
        beachlife_reconstructed_gui_defined_item
        beachlife_reconstructed_gui_context
        beachlife_reconstructed_gui_integer
        beachlife_resource_pool_host)
    add_dependencies(beachlife_reconstructed_gui_parse_define verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_gui_parse_define PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_gui_parse_define PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_gui_parse_define PRIVATE -fno-lto)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(reconstructed_gui_parse_define_tests tests/reconstructed_gui_parse_define.cpp)
        target_link_libraries(reconstructed_gui_parse_define_tests PRIVATE
            beachlife_reconstructed_gui_parse_define)
        set_target_properties(reconstructed_gui_parse_define_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_gui_parse_define_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_gui_parse_define_tests PRIVATE -fno-lto)
        add_test(NAME reconstructed_gui_parse_define COMMAND reconstructed_gui_parse_define_tests)
        add_test(NAME reconstructed_gui_parse_define_fatal_brace
            COMMAND reconstructed_gui_parse_define_tests --fatal-brace)
        set_tests_properties(reconstructed_gui_parse_define reconstructed_gui_parse_define_fatal_brace
            PROPERTIES LABELS "reconstructed;experimental;headless" TIMEOUT 30)
        # Preserve the actual Fatal exit code and independently require its
        # cleanup marker; PASS_REGULAR_EXPRESSION must not override a failed exit.
    endif()
endif()
# The complete menu action composes the real close/request subsystem.
# This focused optional target follows the existing test-only return.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_menu_show_main STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_show_main_menu.cpp")
    target_include_directories(beachlife_reconstructed_menu_show_main PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_menu_show_main PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_menu_show_main PUBLIC
        beachlife_reconstructed_close_active_screens)
    add_dependencies(beachlife_reconstructed_menu_show_main verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_menu_show_main PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_menu_show_main PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_menu_show_main PRIVATE -fno-lto)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(reconstructed_menu_show_main_tests tests/reconstructed_menu_show_main.cpp)
        target_link_libraries(reconstructed_menu_show_main_tests PRIVATE
            beachlife_reconstructed_menu_show_main)
        set_target_properties(reconstructed_menu_show_main_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_menu_show_main_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_menu_show_main_tests PRIVATE -fno-lto)
        add_test(NAME reconstructed_menu_show_main COMMAND reconstructed_menu_show_main_tests)
        set_tests_properties(reconstructed_menu_show_main PROPERTIES
            LABELS "reconstructed;experimental;headless" TIMEOUT 30)
    endif()
endif()

# Genuine ImageItem owns a real resource wrapper and retains the complete base.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_gui_image_item STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_image_item_constructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_image_item_destructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_image_item_virtuals.cpp")
    target_include_directories(beachlife_reconstructed_gui_image_item PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_gui_image_item PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_gui_image_item PUBLIC
        beachlife_reconstructed_frame_item)
    add_dependencies(beachlife_reconstructed_gui_image_item verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_gui_image_item PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_gui_image_item PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_gui_image_item PRIVATE -fno-lto)
    endif()

    # Generated mounted resources and the existing guarded native pool backing.
    # No full manager, renderer, game method or platform provider is supplied.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(reconstructed_gui_image_item_tests tests/reconstructed_gui_image_item.cpp)
        target_link_libraries(reconstructed_gui_image_item_tests PRIVATE
            beachlife_reconstructed_gui_image_item)
        set_target_properties(reconstructed_gui_image_item_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_gui_image_item_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_gui_image_item_tests PRIVATE -fno-lto)
        add_test(NAME reconstructed_gui_image_item
            COMMAND reconstructed_gui_image_item_tests
                "${CMAKE_CURRENT_BINARY_DIR}/generated/gui-image-item")
        set_tests_properties(reconstructed_gui_image_item PROPERTIES
            LABELS "reconstructed;experimental;headless" TIMEOUT 30)
    endif()
endif()
# IMAGE parsing selects the genuine Image owner and real parser/resource graph.
# This optional subsystem follows the existing reconstructed-test return.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_gui_parse_image STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_parse_image.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_parse_icon.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_image_item_icon.cpp")
    target_include_directories(beachlife_reconstructed_gui_parse_image PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_gui_parse_image PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_gui_parse_image PUBLIC
        beachlife_reconstructed_gui_image_item
        beachlife_reconstructed_gui_context
        beachlife_reconstructed_gui_rectangle
        beachlife_reconstructed_screen_localization)
    add_dependencies(beachlife_reconstructed_gui_parse_image verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_gui_parse_image PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_gui_parse_image PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_gui_parse_image PRIVATE -fno-lto)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(reconstructed_gui_parse_image_tests tests/reconstructed_gui_parse_image.cpp)
        target_link_libraries(reconstructed_gui_parse_image_tests PRIVATE
            beachlife_reconstructed_gui_parse_image
            beachlife_reconstructed_frame_item)
        set_target_properties(reconstructed_gui_parse_image_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_gui_parse_image_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_gui_parse_image_tests PRIVATE -fno-lto)
        add_test(NAME reconstructed_gui_parse_image COMMAND reconstructed_gui_parse_image_tests
            "${CMAKE_CURRENT_BINARY_DIR}/generated/gui-parse-image")
        add_test(NAME reconstructed_gui_parse_image_fatal_open
            COMMAND reconstructed_gui_parse_image_tests --fatal-open)
        add_test(NAME reconstructed_gui_parse_icon COMMAND reconstructed_gui_parse_image_tests
            --icon "${CMAKE_CURRENT_BINARY_DIR}/generated/gui-parse-icon")
        add_test(NAME reconstructed_gui_parse_icon_fatal_open
            COMMAND reconstructed_gui_parse_image_tests --icon-fatal-open)
        set_tests_properties(reconstructed_gui_parse_image reconstructed_gui_parse_image_fatal_open
            reconstructed_gui_parse_icon reconstructed_gui_parse_icon_fatal_open
            PROPERTIES LABELS "reconstructed;experimental;headless" TIMEOUT 30)
        # Root qualification also checks the real Fatal before marker
        # and absence of the returned marker, preserving the actual exit code.
    endif()
endif()

# Genuine TabItem owns a real resource wrapper and retains the complete base.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_gui_tab_item STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_tab_constructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_tab_destructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_tab_virtuals.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_tab_coordinates.cpp")
    target_include_directories(beachlife_reconstructed_gui_tab_item PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_gui_tab_item PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_gui_tab_item PUBLIC
        beachlife_reconstructed_frame_item)
    add_dependencies(beachlife_reconstructed_gui_tab_item verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_gui_tab_item PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_gui_tab_item PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_gui_tab_item PRIVATE -fno-lto)
    endif()

    # Generated mounted resources and the existing guarded native pool backing.
    # No full manager, renderer, game method or platform provider is supplied.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(reconstructed_gui_tab_item_tests tests/reconstructed_gui_tab_item.cpp)
        target_link_libraries(reconstructed_gui_tab_item_tests PRIVATE
            beachlife_reconstructed_gui_tab_item)
        set_target_properties(reconstructed_gui_tab_item_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_gui_tab_item_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_gui_tab_item_tests PRIVATE -fno-lto)
        add_test(NAME reconstructed_gui_tab_item
            COMMAND reconstructed_gui_tab_item_tests
                "${CMAKE_CURRENT_BINARY_DIR}/generated/gui-tab-item")
        set_tests_properties(reconstructed_gui_tab_item PROPERTIES
            LABELS "reconstructed;experimental;headless" TIMEOUT 30)
    endif()
endif()
# TAB parsing selects the genuine Tab owner and real parser/resource graph.
# This optional subsystem follows the existing reconstructed-test return.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_gui_parse_tab STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_parse_tab.cpp")
    target_include_directories(beachlife_reconstructed_gui_parse_tab PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_gui_parse_tab PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_gui_parse_tab PUBLIC
        beachlife_reconstructed_gui_tab_item
        beachlife_reconstructed_gui_context
        beachlife_reconstructed_gui_rectangle
        beachlife_reconstructed_screen_localization)
    add_dependencies(beachlife_reconstructed_gui_parse_tab verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_gui_parse_tab PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_gui_parse_tab PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_gui_parse_tab PRIVATE -fno-lto)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(reconstructed_gui_parse_tab_tests tests/reconstructed_gui_parse_tab.cpp)
        target_link_libraries(reconstructed_gui_parse_tab_tests PRIVATE
            beachlife_reconstructed_gui_parse_tab
            beachlife_reconstructed_frame_item)
        set_target_properties(reconstructed_gui_parse_tab_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_gui_parse_tab_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_gui_parse_tab_tests PRIVATE -fno-lto)
        add_test(NAME reconstructed_gui_parse_tab COMMAND reconstructed_gui_parse_tab_tests
            "${CMAKE_CURRENT_BINARY_DIR}/generated/gui-parse-tab")
        add_test(NAME reconstructed_gui_parse_tab_fatal_open
            COMMAND reconstructed_gui_parse_tab_tests --fatal-open)
        set_tests_properties(reconstructed_gui_parse_tab reconstructed_gui_parse_tab_fatal_open
            PROPERTIES LABELS "reconstructed;experimental;headless" TIMEOUT 30)
        # Root qualification also checks the real Fatal before marker
        # and absence of the returned marker, preserving the actual exit code.
    endif()
endif()
# MAIN640 selects width exactly 640 and creates a genuine Frame/context owner.
# Frame/resource/font dependencies retain their existing optional subsystem scope.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_gui_parse_main640 STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_parse_main640.cpp")
    target_include_directories(beachlife_reconstructed_gui_parse_main640 PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_gui_parse_main640 PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_gui_parse_main640 PUBLIC
        beachlife_reconstructed_frame_item
        beachlife_reconstructed_gui_context
        beachlife_reconstructed_gui_rectangle)
    add_dependencies(beachlife_reconstructed_gui_parse_main640 verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_gui_parse_main640 PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_gui_parse_main640 PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_gui_parse_main640 PRIVATE -fno-lto)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(reconstructed_gui_parse_main640_tests tests/reconstructed_gui_parse_main640.cpp)
        target_link_libraries(reconstructed_gui_parse_main640_tests PRIVATE
            beachlife_reconstructed_gui_parse_main640)
        set_target_properties(reconstructed_gui_parse_main640_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_gui_parse_main640_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_gui_parse_main640_tests PRIVATE -fno-lto)
        add_test(NAME reconstructed_gui_parse_main640 COMMAND reconstructed_gui_parse_main640_tests)
        add_test(NAME reconstructed_gui_parse_main640_fatal_open
            COMMAND reconstructed_gui_parse_main640_tests --fatal-open)
        set_tests_properties(reconstructed_gui_parse_main640 reconstructed_gui_parse_main640_fatal_open
            PROPERTIES LABELS "reconstructed;experimental;headless" TIMEOUT 30)
        # Qualification checks actual exit0, before/cleanup markers and no
        # returned marker. StartupFatal formats its message but does not print it.
    endif()
endif()

# Distinct owner190 records release genuine Text/base owners through slot zero.
# This optional subsystem follows the existing reconstructed-test return.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_menu_owner190_entry_destroy STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_manager_owner190_entry_destroy.cpp")
    target_include_directories(beachlife_reconstructed_menu_owner190_entry_destroy PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_menu_owner190_entry_destroy PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_menu_owner190_entry_destroy PUBLIC
        beachlife_reconstructed_gui_text_item)
    add_dependencies(beachlife_reconstructed_menu_owner190_entry_destroy verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_menu_owner190_entry_destroy PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_menu_owner190_entry_destroy PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_menu_owner190_entry_destroy PRIVATE -fno-lto)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(reconstructed_menu_owner190_entry_destroy_tests
            tests/reconstructed_menu_owner190_entry_destroy.cpp)
        target_link_libraries(reconstructed_menu_owner190_entry_destroy_tests PRIVATE
            beachlife_reconstructed_menu_owner190_entry_destroy)
        set_target_properties(reconstructed_menu_owner190_entry_destroy_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_menu_owner190_entry_destroy_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_menu_owner190_entry_destroy_tests PRIVATE -fno-lto)
        add_test(NAME reconstructed_menu_owner190_entry_destroy
            COMMAND reconstructed_menu_owner190_entry_destroy_tests)
        set_tests_properties(reconstructed_menu_owner190_entry_destroy PROPERTIES
            LABELS "reconstructed;experimental;headless" TIMEOUT 30)
    endif()
endif()

# Generated files exercise the real mode/progress body over a typed borrowed prefix.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
        AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_executable(reconstructed_game_mode_progress_tests
        tests/reconstructed_game_mode_progress.cpp)
    target_link_libraries(reconstructed_game_mode_progress_tests PRIVATE
        beachlife_reconstructed_game_mode_progress)
    set_target_properties(reconstructed_game_mode_progress_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    target_compile_options(reconstructed_game_mode_progress_tests PRIVATE
        -fno-lto -Wall -Wextra -Wpedantic -Werror)
    target_link_options(reconstructed_game_mode_progress_tests PRIVATE -fno-lto)
    add_test(NAME reconstructed_game_mode_progress COMMAND reconstructed_game_mode_progress_tests)
    set_tests_properties(reconstructed_game_mode_progress PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# The complete Scenario action composes real mode/FILE loading and close/request.
# This focused optional target follows the existing test-only return.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_menu_show_scenario STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_show_scenario.cpp")
    target_include_directories(beachlife_reconstructed_menu_show_scenario PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_menu_show_scenario PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_menu_show_scenario PUBLIC
        beachlife_reconstructed_game_mode_progress
        beachlife_reconstructed_close_active_screens)
    add_dependencies(beachlife_reconstructed_menu_show_scenario verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_menu_show_scenario PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_menu_show_scenario PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_menu_show_scenario PRIVATE -fno-lto)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(reconstructed_menu_show_scenario_tests
            tests/reconstructed_menu_show_scenario.cpp)
        target_link_libraries(reconstructed_menu_show_scenario_tests PRIVATE
            beachlife_reconstructed_menu_show_scenario)
        set_target_properties(reconstructed_menu_show_scenario_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_menu_show_scenario_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_menu_show_scenario_tests PRIVATE -fno-lto)
        add_test(NAME reconstructed_menu_show_scenario COMMAND reconstructed_menu_show_scenario_tests)
        set_tests_properties(reconstructed_menu_show_scenario PROPERTIES
            LABELS "reconstructed;experimental;headless" TIMEOUT 30)
    endif()
endif()

# This real nonvirtual helper completes a RadioItem update dependency.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_item_clear_manager STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_item_clear_manager.cpp")
    target_include_directories(beachlife_reconstructed_item_clear_manager PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_item_clear_manager PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_item_clear_manager PUBLIC
        beachlife_reconstructed_screen_item_complete)
    add_dependencies(beachlife_reconstructed_item_clear_manager verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_item_clear_manager PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_item_clear_manager PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_item_clear_manager PRIVATE -fno-lto)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(reconstructed_item_clear_manager_tests
            tests/reconstructed_item_clear_manager.cpp)
        target_link_libraries(reconstructed_item_clear_manager_tests PRIVATE
            beachlife_reconstructed_item_clear_manager)
        set_target_properties(reconstructed_item_clear_manager_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_item_clear_manager_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_item_clear_manager_tests PRIVATE -fno-lto)
        add_test(NAME reconstructed_item_clear_manager COMMAND reconstructed_item_clear_manager_tests)
        set_tests_properties(reconstructed_item_clear_manager PROPERTIES
            LABELS "reconstructed;experimental;headless" TIMEOUT 30)
    endif()
endif()

# The complete Sandbox callback shares the qualified Scenario owner fixture.
# Append after its existing target; retain the optional reconstructed-test gate.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_menu_play_sandbox STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_play_sandbox.cpp")
    target_include_directories(beachlife_reconstructed_menu_play_sandbox PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_menu_play_sandbox PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_menu_play_sandbox PUBLIC
        beachlife_reconstructed_game_mode_progress
        beachlife_reconstructed_close_active_screens)
    add_dependencies(beachlife_reconstructed_menu_play_sandbox verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_menu_play_sandbox PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_menu_play_sandbox PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_menu_play_sandbox PRIVATE -fno-lto)
    endif()

    if(TARGET reconstructed_menu_show_scenario_tests)
        target_link_libraries(reconstructed_menu_show_scenario_tests PRIVATE
            beachlife_reconstructed_menu_play_sandbox)
    endif()
endif()

if(BUILD_TESTING AND BEACHLIFE_ENABLE_EXPERIMENTAL_GAME
        AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_executable(reconstructed_sea_state_owner_tests
        tests/reconstructed_sea_state_owner.cpp)
    target_link_libraries(reconstructed_sea_state_owner_tests PRIVATE
        beachlife_reconstructed_sea_state_owner)
    set_target_properties(reconstructed_sea_state_owner_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    target_compile_options(reconstructed_sea_state_owner_tests PRIVATE
        -fno-lto -Wall -Wextra -Wpedantic -Werror)
    target_link_options(reconstructed_sea_state_owner_tests PRIVATE -fno-lto)
    add_test(NAME reconstructed_sea_state_owner COMMAND reconstructed_sea_state_owner_tests)
    set_tests_properties(reconstructed_sea_state_owner PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

if(BUILD_TESTING AND BEACHLIFE_ENABLE_EXPERIMENTAL_GAME
        AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_executable(reconstructed_renderer_sea_enabled_tests
        tests/reconstructed_renderer_sea_enabled.cpp)
    target_link_libraries(reconstructed_renderer_sea_enabled_tests PRIVATE
        beachlife_reconstructed_renderer_sea_enabled)
    set_target_properties(reconstructed_renderer_sea_enabled_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    target_compile_options(reconstructed_renderer_sea_enabled_tests PRIVATE
        -fno-lto -Wall -Wextra -Wpedantic -Werror)
    target_link_options(reconstructed_renderer_sea_enabled_tests PRIVATE -fno-lto)
    add_test(NAME reconstructed_renderer_sea_enabled COMMAND reconstructed_renderer_sea_enabled_tests)
    set_tests_properties(reconstructed_renderer_sea_enabled PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

if(BUILD_TESTING AND BEACHLIFE_ENABLE_EXPERIMENTAL_GAME
        AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8
        AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    add_executable(reconstructed_sound_bank_owner_tests
        tests/reconstructed_sound_bank_owner.cpp)
    target_link_libraries(reconstructed_sound_bank_owner_tests PRIVATE
        beachlife_reconstructed_sound_bank)
    set_target_properties(reconstructed_sound_bank_owner_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    target_compile_options(reconstructed_sound_bank_owner_tests PRIVATE
        -fno-lto -Wall -Wextra -Wpedantic -Werror)
    target_link_options(reconstructed_sound_bank_owner_tests PRIVATE -fno-lto)
    add_test(NAME reconstructed_sound_bank_owner COMMAND reconstructed_sound_bank_owner_tests
        "${CMAKE_CURRENT_BINARY_DIR}/generated-sound-bank-owner")
    set_tests_properties(reconstructed_sound_bank_owner PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Storage members can be used independently of the incomplete ListBox widget.
# This subsystem retains the existing optional reconstructed-test selection.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_listbox_storage STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_list_box_entry_range_destructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_list_box_selection_destructor.cpp")
    target_include_directories(beachlife_reconstructed_listbox_storage PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_listbox_storage PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_listbox_storage PUBLIC
        beachlife_reconstructed_screen_list)
    add_dependencies(beachlife_reconstructed_listbox_storage verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_listbox_storage PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_listbox_storage PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_listbox_storage PRIVATE -fno-lto)
    endif()

    if(BUILD_TESTING AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8
            AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
        add_executable(reconstructed_listbox_storage_tests tests/reconstructed_listbox_storage.cpp)
        target_link_libraries(reconstructed_listbox_storage_tests PRIVATE
            beachlife_reconstructed_listbox_storage
            beachlife_reconstructed_screen_child_ring)
        set_target_properties(reconstructed_listbox_storage_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_listbox_storage_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_listbox_storage_tests PRIVATE
            -fno-lto "LINKER:--wrap=_ZdlPv")
        add_test(NAME reconstructed_listbox_storage COMMAND reconstructed_listbox_storage_tests)
        set_tests_properties(reconstructed_listbox_storage PROPERTIES
            LABELS "reconstructed;experimental;headless" TIMEOUT 30)
    endif()
endif()

if(BUILD_TESTING AND BEACHLIFE_ENABLE_EXPERIMENTAL_GAME
        AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(reconstructed_listbox_selection_find_tests
        tests/reconstructed_listbox_selection_find.cpp)
    target_link_libraries(reconstructed_listbox_selection_find_tests PRIVATE
        beachlife_reconstructed_listbox_selection_find)
    set_target_properties(reconstructed_listbox_selection_find_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    target_compile_options(reconstructed_listbox_selection_find_tests PRIVATE
        -fno-lto -Wall -Wextra -Wpedantic -Werror)
    target_link_options(reconstructed_listbox_selection_find_tests PRIVATE -fno-lto)
    add_test(NAME reconstructed_listbox_selection_find
        COMMAND reconstructed_listbox_selection_find_tests)
    set_tests_properties(reconstructed_listbox_selection_find PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()
# ScreenSelect uses the complete qualified request, Dialog and active-list path.
# Keep this subsystem below the existing optional reconstructed-test return.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_screen_select STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_manager_select.cpp")
    target_include_directories(beachlife_reconstructed_screen_select PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_screen_select PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_screen_select PUBLIC
        beachlife_reconstructed_screen_request)
    add_dependencies(beachlife_reconstructed_screen_select verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_screen_select PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_screen_select PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_screen_select PRIVATE -fno-lto)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(reconstructed_screen_select_tests tests/reconstructed_screen_select.cpp)
        target_link_libraries(reconstructed_screen_select_tests PRIVATE
            beachlife_reconstructed_screen_select)
        set_target_properties(reconstructed_screen_select_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_screen_select_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_screen_select_tests PRIVATE -fno-lto)
        add_test(NAME reconstructed_screen_select COMMAND reconstructed_screen_select_tests)
        set_tests_properties(reconstructed_screen_select PROPERTIES
            LABELS "reconstructed;experimental;headless" TIMEOUT 30)
    endif()
endif()
# ActivateScreens uses the qualified request, Dialog and active-list path.
# Keep this subsystem below the existing optional reconstructed-test return.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_screen_activate STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_activate.cpp")
    target_include_directories(beachlife_reconstructed_screen_activate PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_screen_activate PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_screen_activate PUBLIC
        beachlife_reconstructed_screen_request)
    add_dependencies(beachlife_reconstructed_screen_activate verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_screen_activate PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_screen_activate PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_screen_activate PRIVATE -fno-lto)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(reconstructed_screen_activate_tests tests/reconstructed_screen_activate.cpp)
        target_link_libraries(reconstructed_screen_activate_tests PRIVATE
            beachlife_reconstructed_screen_activate)
        set_target_properties(reconstructed_screen_activate_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_screen_activate_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_screen_activate_tests PRIVATE -fno-lto)
        add_test(NAME reconstructed_screen_activate COMMAND reconstructed_screen_activate_tests)
        set_tests_properties(reconstructed_screen_activate PROPERTIES
            LABELS "reconstructed;experimental;headless" TIMEOUT 30)
    endif()
endif()

# Complete screen clipping uses the existing sole renderer-global owner.
# This subsystem remains below the optional reconstructed-test return.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_screen_clip STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_clip.cpp")
    target_include_directories(beachlife_reconstructed_screen_clip PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_screen_clip PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_screen_clip PUBLIC
        beachlife_reconstructed_gui_rectangle)
    add_dependencies(beachlife_reconstructed_screen_clip verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_screen_clip PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_screen_clip PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_screen_clip PRIVATE -fno-lto)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(reconstructed_screen_clip_tests tests/reconstructed_screen_clip.cpp)
        target_link_libraries(reconstructed_screen_clip_tests PRIVATE
            beachlife_reconstructed_screen_clip)
        set_target_properties(reconstructed_screen_clip_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_screen_clip_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_screen_clip_tests PRIVATE -fno-lto)
        add_test(NAME reconstructed_screen_clip COMMAND reconstructed_screen_clip_tests)
        set_tests_properties(reconstructed_screen_clip PROPERTIES
            LABELS "reconstructed;experimental;headless" TIMEOUT 30)
    endif()
endif()
# Optional Linux ownership fixture for the genuine idle Mode6 owner.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_executable(reconstructed_screen_mode6_owner_tests tests/reconstructed_screen_mode6_owner.cpp)
        target_link_libraries(reconstructed_screen_mode6_owner_tests PRIVATE
            beachlife_reconstructed_screen_mode6)
        set_target_properties(reconstructed_screen_mode6_owner_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_screen_mode6_owner_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_screen_mode6_owner_tests PRIVATE -fno-lto)
        add_test(NAME reconstructed_screen_mode6_owner COMMAND reconstructed_screen_mode6_owner_tests)
        set_tests_properties(reconstructed_screen_mode6_owner PROPERTIES
            LABELS "reconstructed;experimental;headless" TIMEOUT 30)
    endif()
endif()


# The clock takes an explicit tick value; this does not emulate an OS clock.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
        AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_executable(reconstructed_frame_clock_owner_tests
        tests/reconstructed_frame_clock_owner.cpp)
    target_link_libraries(reconstructed_frame_clock_owner_tests PRIVATE
        beachlife_reconstructed_frame_clock)
    set_target_properties(reconstructed_frame_clock_owner_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    target_compile_options(reconstructed_frame_clock_owner_tests PRIVATE
        -fno-lto -Wall -Wextra -Wpedantic -Werror)
    target_link_options(reconstructed_frame_clock_owner_tests PRIVATE -fno-lto)
    add_test(NAME reconstructed_frame_clock_owner COMMAND reconstructed_frame_clock_owner_tests)
    set_tests_properties(reconstructed_frame_clock_owner PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# InputMotion uses the existing real cursor, TGA/VFS and singleton owners.
# Its dependencies remain within the optional reconstructed-test selection.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_input_motion_owner STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_input_motion_constructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_input_motion_destructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_input_motion_globals.cpp")
    target_include_directories(beachlife_reconstructed_input_motion_owner PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_input_motion_owner PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_input_motion_owner PUBLIC
        beachlife_reconstructed_tga_files
        beachlife_reconstructed_cursor
        beachlife_reconstructed_gui_rectangle
        beachlife_reconstructed_frame_clock)
    add_dependencies(beachlife_reconstructed_input_motion_owner verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_input_motion_owner PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_input_motion_owner PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_input_motion_owner PRIVATE -fno-lto)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8
        AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
        add_executable(reconstructed_input_motion_owner_tests
            tests/reconstructed_input_motion_owner.cpp)
        target_link_libraries(reconstructed_input_motion_owner_tests PRIVATE
            beachlife_reconstructed_input_motion_owner)
        set_target_properties(reconstructed_input_motion_owner_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_input_motion_owner_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_input_motion_owner_tests PRIVATE -fno-lto)
        add_test(NAME reconstructed_input_motion_owner
            COMMAND reconstructed_input_motion_owner_tests
                "${CMAKE_CURRENT_BINARY_DIR}/input-motion-owner-fixture")
        set_tests_properties(reconstructed_input_motion_owner PROPERTIES
            LABELS "reconstructed;experimental;headless" TIMEOUT 60)
    endif()
endif()

if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_executable(reconstructed_gui_float_tests tests/reconstructed_gui_float.cpp)
    target_link_libraries(reconstructed_gui_float_tests PRIVATE beachlife_reconstructed_gui_float)
    set_target_properties(reconstructed_gui_float_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(reconstructed_gui_float_tests PRIVATE /GL- /W4 /WX)
    else()
        target_compile_options(reconstructed_gui_float_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_gui_float_tests PRIVATE -fno-lto)
    endif()
    add_test(NAME reconstructed_gui_float COMMAND reconstructed_gui_float_tests)
    set_tests_properties(reconstructed_gui_float PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()

# Complete language lookup owner and genuine fixture lifetimes.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    target_sources(beachlife_reconstructed_screen_localization PRIVATE
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_text_lookup_constructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_text_lookup_destructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_text_table_reserve.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_text_table_destructor.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_text_storage.cpp")
    target_link_libraries(beachlife_reconstructed_screen_localization PUBLIC
        beachlife_reconstructed_files beachlife_reconstructed_sound_bank)

    # Initial qualification is Linux x86-64 only. The existing prime-table
    # member is selected from the real sound-bank archive, never recompiled here.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
        add_executable(reconstructed_text_lookup_owner_tests
            tests/reconstructed_text_lookup_owner.cpp)
        target_link_libraries(reconstructed_text_lookup_owner_tests PRIVATE
            beachlife_reconstructed_screen_localization)
        target_compile_features(reconstructed_text_lookup_owner_tests PRIVATE cxx_std_20)
        set_target_properties(reconstructed_text_lookup_owner_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_text_lookup_owner_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_text_lookup_owner_tests PRIVATE -fno-lto)
        add_test(NAME reconstructed_text_lookup_owner
            COMMAND reconstructed_text_lookup_owner_tests
                "${CMAKE_CURRENT_BINARY_DIR}/generated-text-lookup-owner")
        set_tests_properties(reconstructed_text_lookup_owner PROPERTIES
            LABELS "reconstructed;experimental;headless" TIMEOUT 30)
    endif()
endif()

# Append after the existing experimental reconstructed targets. This first
# capability remains below the reconstructed-test return: no production claim.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    add_library(beachlife_reconstructed_renderer_owner STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_constructor_native.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_destructor_native.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_display_initialize_native.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_display_mode_callback.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_registration_clear.cpp")
    target_include_directories(beachlife_reconstructed_renderer_owner PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}" "${CMAKE_SOURCE_DIR}/src")
    target_compile_features(beachlife_reconstructed_renderer_owner PUBLIC cxx_std_20)
    target_compile_definitions(beachlife_reconstructed_renderer_owner PUBLIC
        BEACHLIFE_DECOMP_RENDERER_QUEUE=1)
    target_link_libraries(beachlife_reconstructed_renderer_owner PUBLIC
        beachlife_reconstructed_surface_create
        beachlife_reconstructed_render_masks
        beachlife_reconstructed_sea_state_owner
        beachlife_reconstructed_tiff_loader
        beachlife_renderer_display)
    add_dependencies(beachlife_reconstructed_renderer_owner verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_renderer_owner PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_renderer_owner PRIVATE /GL-)
    else()
        target_compile_options(beachlife_reconstructed_renderer_owner PRIVATE -fno-lto)
    endif()

    # Exact R803 fixture and control; no Present/capture object in this target.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 8
            AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
        add_executable(reconstructed_renderer_owner_tests tests/reconstructed_renderer_owner.cpp)
        target_link_libraries(reconstructed_renderer_owner_tests PRIVATE
            beachlife_reconstructed_renderer_owner)
        set_target_properties(reconstructed_renderer_owner_tests PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        target_compile_options(reconstructed_renderer_owner_tests PRIVATE
            -fno-lto -Wall -Wextra -Wpedantic -Werror)
        target_link_options(reconstructed_renderer_owner_tests PRIVATE -fno-lto)
        add_test(NAME reconstructed_renderer_owner
            COMMAND reconstructed_renderer_owner_tests
                "${CMAKE_CURRENT_BINARY_DIR}/generated-renderer-owner")
        set_tests_properties(reconstructed_renderer_owner PROPERTIES
            LABELS "reconstructed;experimental;renderer-owner;x11" TIMEOUT 30)
    endif()
    # Insert inside the native-experimental owner block, after its real target exists.
    foreach(BEACHLIFE_RENDERER_OWNER_FIXTURE IN ITEMS
        reconstructed_auxiliary_text_tests
        reconstructed_close_active_screens_tests
        reconstructed_dialog_item_tests
        reconstructed_font_data_owner_tests
        reconstructed_font_drawing_tests
        reconstructed_font_glyphs_tests
        reconstructed_font_measurement_tests
        reconstructed_frame_fill_styles_tests
        reconstructed_frame_item_tests
        reconstructed_gui_image_item_tests
        reconstructed_gui_parse_dialog_tests
        reconstructed_gui_parse_font_tests
        reconstructed_gui_parse_image_tests
        reconstructed_gui_parse_main640_tests
        reconstructed_gui_parse_main_tests
        reconstructed_gui_parse_tab_tests
        reconstructed_gui_rectangle_tests
        reconstructed_gui_tab_item_tests
        reconstructed_gui_text_item_tests
        reconstructed_input_motion_owner_tests
        reconstructed_menu_show_main_tests
        reconstructed_menu_show_scenario_tests
        reconstructed_renderer_clip_tests
        reconstructed_renderer_default_tests
        reconstructed_renderer_line_tests
        reconstructed_renderer_rectangle_fill_tests
        reconstructed_renderer_rectangle_queue_tests
        reconstructed_renderer_sea_enabled_tests
        reconstructed_renderer_tga_draw_tests
        reconstructed_renderer_word60_tests
        reconstructed_resource_file_producer_tests
        reconstructed_resource_find_tests
        reconstructed_resource_owner_tests
        reconstructed_resource_palette_modifiers_tests
        reconstructed_screen_activate_tests
        reconstructed_screen_clip_tests
        reconstructed_screen_item_complete_tests
        reconstructed_screen_request_tests
        reconstructed_screen_select_tests
        reconstructed_surface_create_tests
        reconstructed_surface_pixels_tests
        reconstructed_surfaces_tests
        reconstructed_tga_widget_tests
    )
        if(TARGET ${BEACHLIFE_RENDERER_OWNER_FIXTURE})
            target_link_libraries(${BEACHLIFE_RENDERER_OWNER_FIXTURE} PRIVATE
                beachlife_reconstructed_renderer_owner)
        endif()
    endforeach()

    # These sources now require actual x11 geometry; no dummy/platform fallback.
    foreach(BEACHLIFE_RENDERER_OWNER_CASE IN ITEMS
        reconstructed_auxiliary_text
        reconstructed_close_active_screens
        reconstructed_dialog_item
        reconstructed_font_data_owner
        reconstructed_font_drawing
        reconstructed_font_glyphs
        reconstructed_font_measurement
        reconstructed_frame_fill_styles
        reconstructed_frame_item
        reconstructed_gui_image_item
        reconstructed_gui_parse_dialog
        reconstructed_gui_parse_font
        reconstructed_gui_parse_image
        reconstructed_gui_parse_main640
        reconstructed_gui_parse_main
        reconstructed_gui_parse_tab
        reconstructed_gui_rectangle
        reconstructed_gui_tab_item
        reconstructed_gui_text_item
        reconstructed_input_motion_owner
        reconstructed_menu_show_main
        reconstructed_menu_show_scenario
        reconstructed_renderer_clip
        reconstructed_renderer_default
        reconstructed_renderer_line
        reconstructed_renderer_rectangle_fill
        reconstructed_renderer_rectangle_queue
        reconstructed_renderer_sea_enabled
        reconstructed_renderer_tga_draw
        reconstructed_renderer_word60
        reconstructed_resource_file_producer
        reconstructed_resource_find
        reconstructed_resource_owner
        reconstructed_resource_palette_modifiers
        reconstructed_screen_activate
        reconstructed_screen_clip
        reconstructed_screen_item_complete
        reconstructed_screen_request
        reconstructed_screen_select
        reconstructed_surface_create
        reconstructed_surface_pixels
        reconstructed_surfaces
        reconstructed_tga_widget
        reconstructed_gui_rectangle_fatal_right
        reconstructed_gui_rectangle_fatal_bottom
        reconstructed_gui_rectangle_fatal_reversed
    )
        if(TEST ${BEACHLIFE_RENDERER_OWNER_CASE})
            set_tests_properties(${BEACHLIFE_RENDERER_OWNER_CASE} PROPERTIES
                LABELS "reconstructed;experimental;renderer-owner;x11")
            if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8
                    OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
                set_tests_properties(${BEACHLIFE_RENDERER_OWNER_CASE} PROPERTIES DISABLED TRUE)
            endif()
        endif()
    endforeach()
endif()

# The initial generated failure case uses Linux's real /dev/full device.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(reconstructed_jpeg_export_tests tests/reconstructed_jpeg_export.cpp)
    target_include_directories(reconstructed_jpeg_export_tests SYSTEM PRIVATE
        "${BEACHLIFE_PREPARED}/stb")
    target_link_libraries(reconstructed_jpeg_export_tests PRIVATE
        beachlife_reconstructed_jpeg_export beachlife_reconstructed_tga)
    add_dependencies(reconstructed_jpeg_export_tests verify_prepared)
    set_target_properties(reconstructed_jpeg_export_tests PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    target_compile_options(reconstructed_jpeg_export_tests PRIVATE
        -fno-lto -Wall -Wextra -Wpedantic -Werror)
    target_link_options(reconstructed_jpeg_export_tests PRIVATE -fno-lto)
    set(BEACHLIFE_RECONSTRUCTED_JPEG_FIXTURE_ROOT
        "${CMAKE_CURRENT_BINARY_DIR}/jpeg-export-fixture" CACHE PATH
        "Generated JPEG export output directory")
    add_test(NAME reconstructed_jpeg_export COMMAND reconstructed_jpeg_export_tests
        "${BEACHLIFE_RECONSTRUCTED_JPEG_FIXTURE_ROOT}" "/dev/full")
    set_tests_properties(reconstructed_jpeg_export PROPERTIES
        LABELS "reconstructed;experimental;headless" TIMEOUT 30)
endif()
