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
    --decomp-revision "${BEACHLIFE_DECOMP_REVISION}" --decomp-only)
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

# Actual saved-placement allocation, insertion and lookup share one source
# closure. Generated fixtures own borrowed records and deterministic providers.
add_library(beachlife_reconstructed_screen_placements STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/startup_placement_node.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_placement_insert.cpp"
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
