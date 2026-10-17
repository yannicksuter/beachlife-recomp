set(BEACHLIFE_PREPARED "${CMAKE_BINARY_DIR}/prepared")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/prepare_sources.py"
    --output "${BEACHLIFE_PREPARED}" COMMAND_ERROR_IS_FATAL ANY)
add_custom_target(verify_prepared
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/prepare_sources.py"
        --output "${BEACHLIFE_PREPARED}" --verify
    COMMENT "Verify dependency pins, patches and prepared source"
    VERBATIM)

function(beachlife_add_sdl)
    if(POLICY CMP0219)
        set(CMAKE_POLICY_DEFAULT_CMP0219 OLD)
    endif()
    set(SDL_SHARED OFF)
    set(SDL_STATIC ON)
    set(SDL_TEST_LIBRARY OFF)
    set(SDL_TESTS OFF)
    set(SDL_EXAMPLES OFF)
    set(SDL_INSTALL OFF)
    add_subdirectory("${BEACHLIFE_PREPARED}/sdl" "${CMAKE_BINARY_DIR}/extern/sdl" EXCLUDE_FROM_ALL)
endfunction()
beachlife_add_sdl()
add_dependencies(SDL3-static verify_prepared)

set(IMGUI_DIR "${BEACHLIFE_PREPARED}/imgui")
add_library(beachlife_ui STATIC
    "${IMGUI_DIR}/imgui.cpp" "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp" "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp"
    "${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp"
    src/launcher/image.cpp)
target_include_directories(beachlife_ui SYSTEM PUBLIC
    "${IMGUI_DIR}" "${IMGUI_DIR}/backends" "${IMGUI_DIR}/misc/cpp")
target_include_directories(beachlife_ui SYSTEM PRIVATE "${BEACHLIFE_PREPARED}/stb")
target_link_libraries(beachlife_ui PUBLIC SDL3::SDL3)
target_compile_features(beachlife_ui PUBLIC cxx_std_20)
add_dependencies(beachlife_ui verify_prepared)

target_sources(beachlife PRIVATE src/launcher/launcher.cpp src/launcher/settings.cpp)
target_include_directories(beachlife PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(beachlife PRIVATE beachlife_ui)
set_property(TARGET beachlife APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_SOURCE_DIR}/assets/launcher/header.jpg"
    "${IMGUI_DIR}/misc/fonts/Roboto-Medium.ttf")

# SDL uses the bundle Resources directory on macOS and the executable directory elsewhere.
set(BEACHLIFE_RESOURCE_DIR "$<TARGET_FILE_DIR:beachlife>/assets/launcher")
if(APPLE)
    set(BEACHLIFE_RESOURCE_DIR "$<TARGET_FILE_DIR:beachlife>/../Resources/assets/launcher")
endif()
add_custom_command(TARGET beachlife POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${BEACHLIFE_RESOURCE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/launcher/header.jpg"
        "${IMGUI_DIR}/misc/fonts/Roboto-Medium.ttf"
        "${BEACHLIFE_RESOURCE_DIR}"
    VERBATIM)
