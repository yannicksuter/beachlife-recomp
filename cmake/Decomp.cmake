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

add_library(beachlife_reconstructed_surface_pixels STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/startup_surface_pixel_access.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/surface_error_sink.cpp")
target_include_directories(beachlife_reconstructed_surface_pixels PUBLIC
    "${BEACHLIFE_DECOMP_SOURCE}")
target_compile_features(beachlife_reconstructed_surface_pixels PUBLIC cxx_std_20)
set_target_properties(beachlife_reconstructed_surface_pixels PROPERTIES CXX_EXTENSIONS OFF)
add_dependencies(beachlife_reconstructed_surface_pixels verify_reconstructed_source)

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
        beachlife_reconstructed_surface_pixels
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
