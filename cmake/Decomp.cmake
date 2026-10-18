option(BEACHLIFE_BUILD_RECONSTRUCTED_TESTS "Build tests for reconstructed subsystems" OFF)
if(NOT BEACHLIFE_BUILD_RECONSTRUCTED_TESTS)
    return()
endif()
if(NOT BUILD_TESTING)
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

add_library(beachlife_reconstructed_files STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/file_size.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/file_loader.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/ble_loader.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_read.cpp"
    "${CMAKE_SOURCE_DIR}/src/runtime/retail_files.cpp")
target_include_directories(beachlife_reconstructed_files PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(beachlife_reconstructed_files PUBLIC cxx_std_20)
target_link_libraries(beachlife_reconstructed_files PUBLIC beachlife_data Threads::Threads)
set_target_properties(beachlife_reconstructed_files PROPERTIES CXX_EXTENSIONS OFF)
add_dependencies(beachlife_reconstructed_files verify_reconstructed_source)

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

add_library(beachlife_reconstructed_config_tokens STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_cleanup.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_token_reset.cpp")
target_include_directories(beachlife_reconstructed_config_tokens PUBLIC "${BEACHLIFE_DECOMP_SOURCE}")
target_compile_features(beachlife_reconstructed_config_tokens PUBLIC cxx_std_20)
set_target_properties(beachlife_reconstructed_config_tokens PROPERTIES CXX_EXTENSIONS OFF)
add_dependencies(beachlife_reconstructed_config_tokens verify_reconstructed_source)

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

add_library(beachlife_reconstructed_config_allocation STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_token_allocation.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_token_reset.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_config_cleanup.cpp")
target_include_directories(beachlife_reconstructed_config_allocation PUBLIC "${BEACHLIFE_DECOMP_SOURCE}")
target_compile_features(beachlife_reconstructed_config_allocation PUBLIC cxx_std_20)
set_target_properties(beachlife_reconstructed_config_allocation PROPERTIES CXX_EXTENSIONS OFF)
add_dependencies(beachlife_reconstructed_config_allocation verify_reconstructed_source)

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
