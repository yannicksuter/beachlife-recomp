option(BEACHLIFE_BUILD_RECONSTRUCTED_TESTS "Build tests for reconstructed TGA helpers" OFF)
if(NOT BEACHLIFE_BUILD_RECONSTRUCTED_TESTS)
    return()
endif()
if(NOT BUILD_TESTING)
    message(FATAL_ERROR "BEACHLIFE_BUILD_RECONSTRUCTED_TESTS requires BUILD_TESTING")
endif()

# The selected commit must exist locally; configuration never fetches source.
set(BEACHLIFE_DECOMP_REVISION "c5ac13dc11dc5cb7b4f35ebdebd188b3efd7fda6")
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
