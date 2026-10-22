# Real renderer startup/presentation libraries, selected once for both the
# optional fixtures and experimental consumers with reconstructed tests disabled.
# This file owns no executable and never selects comparison or fixture providers.
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
add_library(beachlife_reconstructed_surfaces STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/startup_surface_position.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_surface_cleanup.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_surface_indexed_cleanup.cpp")
target_include_directories(beachlife_reconstructed_surfaces PUBLIC "${BEACHLIFE_DECOMP_SOURCE}")
target_compile_features(beachlife_reconstructed_surfaces PUBLIC cxx_std_20)
set_target_properties(beachlife_reconstructed_surfaces PROPERTIES CXX_EXTENSIONS OFF)
add_dependencies(beachlife_reconstructed_surfaces verify_reconstructed_source)
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    target_link_libraries(beachlife_reconstructed_surfaces PUBLIC beachlife_renderer_display)
endif()
add_library(beachlife_reconstructed_gui_integer STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_integer.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_whitespace.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_text.cpp")
target_include_directories(beachlife_reconstructed_gui_integer PUBLIC
    "${BEACHLIFE_DECOMP_SOURCE}")
target_compile_features(beachlife_reconstructed_gui_integer PUBLIC cxx_std_20)
add_dependencies(beachlife_reconstructed_gui_integer verify_reconstructed_source)
set_target_properties(beachlife_reconstructed_gui_integer PROPERTIES CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
if(MSVC)
    target_compile_options(beachlife_reconstructed_gui_integer PRIVATE /GL-)
endif()
if(NOT (MSVC))
    target_compile_options(beachlife_reconstructed_gui_integer PRIVATE -fno-lto)
endif()
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
    target_sources(beachlife_reconstructed_tga PRIVATE
        "${BEACHLIFE_DECOMP_SOURCE}/tga_image_allocation.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/tga_buffer_parser.cpp")
    set_target_properties(beachlife_reconstructed_tga PROPERTIES CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_tga PRIVATE /GL-)
    endif()
    if(NOT (MSVC))
        target_compile_options(beachlife_reconstructed_tga PRIVATE -fno-lto)
    endif()
    target_sources(beachlife_reconstructed_tga PRIVATE
        "${BEACHLIFE_DECOMP_SOURCE}/tga_palette_convert.cpp")
    add_library(beachlife_reconstructed_tiff_conversions STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/tiff_image_conversions.cpp")
    target_include_directories(beachlife_reconstructed_tiff_conversions PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_tiff_conversions PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_tiff_conversions PUBLIC
        beachlife_reconstructed_tga)
    add_dependencies(beachlife_reconstructed_tiff_conversions verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_tiff_conversions PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_tiff_conversions PRIVATE /GL-)
    endif()
    if(NOT (MSVC))
        target_compile_options(beachlife_reconstructed_tiff_conversions PRIVATE -fno-lto)
    endif()
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
    if(MSVC)
        target_compile_options(beachlife_reconstructed_tiff_loader PRIVATE /GL-)
    endif()
    if(NOT (MSVC))
        target_compile_options(beachlife_reconstructed_tiff_loader PRIVATE -fno-lto)
    endif()
    add_library(beachlife_reconstructed_surface_create STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_surface_create.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_surface_initialize.cpp")
    target_include_directories(beachlife_reconstructed_surface_create PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_surface_create PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_surface_create PUBLIC
        beachlife_reconstructed_surfaces
        beachlife_reconstructed_renderer_primitives
        beachlife_renderer_display)
    add_dependencies(beachlife_reconstructed_surface_create verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_surface_create PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_surface_create PRIVATE /GL-)
    endif()
    if(NOT (MSVC))
        target_compile_options(beachlife_reconstructed_surface_create PRIVATE -fno-lto)
    endif()
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
    endif()
    if(NOT (MSVC))
        target_compile_options(beachlife_reconstructed_renderer_owner PRIVATE -fno-lto)
    endif()
    add_library(beachlife_reconstructed_renderer_capture STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_capture_native.cpp")
    target_include_directories(beachlife_reconstructed_renderer_capture PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_renderer_capture PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_renderer_capture PUBLIC
        beachlife_reconstructed_renderer_owner
        beachlife_reconstructed_surface_pixels
        beachlife_reconstructed_tga
        beachlife_reconstructed_jpeg_export)
    add_dependencies(beachlife_reconstructed_renderer_capture verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_renderer_capture PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_renderer_capture PRIVATE /GL-)
    endif()
    if(NOT (MSVC))
        target_compile_options(beachlife_reconstructed_renderer_capture PRIVATE -fno-lto)
    endif()
    add_library(beachlife_reconstructed_renderer_present STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_present_native.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_present_globals.cpp")
    target_include_directories(beachlife_reconstructed_renderer_present PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_renderer_present PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_renderer_present PUBLIC
        beachlife_reconstructed_renderer_capture
        beachlife_reconstructed_renderer_present_effect)
    add_dependencies(beachlife_reconstructed_renderer_present verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_renderer_present PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_renderer_present PRIVATE /GL-)
    endif()
    if(NOT (MSVC))
        target_compile_options(beachlife_reconstructed_renderer_present PRIVATE -fno-lto)
    endif()
    add_library(beachlife_reconstructed_renderer_blits STATIC
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_blit_surface.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_blit_surface_region.cpp"
        "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_rectangle_surface.cpp")
    target_include_directories(beachlife_reconstructed_renderer_blits PUBLIC
        "${BEACHLIFE_DECOMP_SOURCE}")
    target_compile_features(beachlife_reconstructed_renderer_blits PUBLIC cxx_std_20)
    target_link_libraries(beachlife_reconstructed_renderer_blits PUBLIC
        beachlife_reconstructed_surface_create
        beachlife_reconstructed_surface_operations
        beachlife_reconstructed_surface_pixels
        beachlife_renderer_display)
    add_dependencies(beachlife_reconstructed_renderer_blits verify_reconstructed_source)
    set_target_properties(beachlife_reconstructed_renderer_blits PROPERTIES
        CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
    if(MSVC)
        target_compile_options(beachlife_reconstructed_renderer_blits PRIVATE /GL-)
    endif()
    if(NOT (MSVC))
        target_compile_options(beachlife_reconstructed_renderer_blits PRIVATE -fno-lto)
    endif()
endif()

# Preserve the actual prepared-prefix selection formerly applied below return.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
        target_compile_definitions(beachlife_reconstructed_gui_integer PUBLIC
            BEACHLIFE_DECOMP_SCREEN_ACTIVATION=1 BEACHLIFE_DECOMP_AUXILIARY_TEXT=1)
        target_compile_definitions(beachlife_reconstructed_surfaces PUBLIC
            BEACHLIFE_DECOMP_RENDERER_QUEUE=1)
endif()

add_library(beachlife_reconstructed_renderer_primitives STATIC
    "${BEACHLIFE_DECOMP_SOURCE}/startup_gui_rectangle.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_screen_rectangle.cpp"
    "${BEACHLIFE_DECOMP_SOURCE}/startup_fatal.cpp")
target_include_directories(beachlife_reconstructed_renderer_primitives PUBLIC
    "${BEACHLIFE_DECOMP_SOURCE}")
target_compile_features(beachlife_reconstructed_renderer_primitives PUBLIC cxx_std_20)
target_link_libraries(beachlife_reconstructed_renderer_primitives PUBLIC
    beachlife_reconstructed_gui_integer)
add_dependencies(beachlife_reconstructed_renderer_primitives verify_reconstructed_source)
set_target_properties(beachlife_reconstructed_renderer_primitives PROPERTIES
    CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
if(MSVC)
        target_compile_options(beachlife_reconstructed_renderer_primitives PRIVATE /GL-)
else()
        target_compile_options(beachlife_reconstructed_renderer_primitives PRIVATE -fno-lto)
endif()
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
        target_compile_definitions(beachlife_reconstructed_renderer_primitives PUBLIC
            BEACHLIFE_DECOMP_AUXILIARY_TEXT=1 BEACHLIFE_DECOMP_COMPLETE_SCREEN_ITEM=1
            BEACHLIFE_DECOMP_RENDERER_QUEUE=1 BEACHLIFE_DECOMP_SCREEN_ACTIVATION=1)
endif()

# These unchanged real source bodies used to belong to the Dialog/font archives.
if(BEACHLIFE_ENABLE_EXPERIMENTAL_GAME)
        add_library(beachlife_reconstructed_surface_operations STATIC
            "${BEACHLIFE_DECOMP_SOURCE}/startup_surface_prepare.cpp"
            "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_surface_unlock.cpp"
            "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_surface_pitch.cpp"
            "${BEACHLIFE_DECOMP_SOURCE}/startup_renderer_surface_position.cpp")
        target_include_directories(beachlife_reconstructed_surface_operations PUBLIC
            "${BEACHLIFE_DECOMP_SOURCE}")
        target_compile_features(beachlife_reconstructed_surface_operations PUBLIC cxx_std_20)
        target_compile_definitions(beachlife_reconstructed_surface_operations PUBLIC
            BEACHLIFE_DECOMP_AUXILIARY_TEXT=1 BEACHLIFE_DECOMP_COMPLETE_SCREEN_ITEM=1
            BEACHLIFE_DECOMP_RENDERER_QUEUE=1 BEACHLIFE_DECOMP_SCREEN_ACTIVATION=1)
        target_link_libraries(beachlife_reconstructed_surface_operations PUBLIC
            beachlife_reconstructed_surface_pixels beachlife_renderer_display)
        add_dependencies(beachlife_reconstructed_surface_operations verify_reconstructed_source)
        set_target_properties(beachlife_reconstructed_surface_operations PROPERTIES
            CXX_EXTENSIONS OFF INTERPROCEDURAL_OPTIMIZATION FALSE)
        if(MSVC)
                target_compile_options(beachlife_reconstructed_surface_operations PRIVATE /GL-)
        else()
                target_compile_options(beachlife_reconstructed_surface_operations PRIVATE -fno-lto)
        endif()
endif()
