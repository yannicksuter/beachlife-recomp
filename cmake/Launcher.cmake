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

# The launcher's logic without its interface: settings, and the update check and
# in-place updates. HTTPS goes through the system (WinHTTP, or the system's libcurl
# loaded at run time); unpacking packages uses the pinned zlib (cmake/Cabinet.cmake).
find_package(Threads REQUIRED)
add_library(beachlife_launcher_core STATIC
    src/launcher/settings.cpp
    src/launcher/update_check.cpp src/launcher/update_install.cpp src/launcher/update_service.cpp
    src/platform/http_client.cpp src/platform/json.cpp src/platform/sha256.cpp)
target_include_directories(beachlife_launcher_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(beachlife_launcher_core PUBLIC cxx_std_20)
set_target_properties(beachlife_launcher_core PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(beachlife_launcher_core PRIVATE ZLIB::ZLIBSTATIC PUBLIC Threads::Threads)
if(WIN32)
    target_link_libraries(beachlife_launcher_core PRIVATE winhttp)
else()
    target_link_libraries(beachlife_launcher_core PRIVATE ${CMAKE_DL_LIBS})
endif()
if(MSVC)
    target_compile_options(beachlife_launcher_core PRIVATE /W4 /WX)
else()
    target_compile_options(beachlife_launcher_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

target_sources(beachlife PRIVATE src/launcher/launcher.cpp src/launcher/ui.cpp)
target_include_directories(beachlife PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(beachlife PRIVATE beachlife_ui beachlife_launcher_core)

if(BUILD_TESTING)
    # Packages written the way tools/package_build.py writes them, then the C++ checks.
    add_executable(launcher_update_tests tests/launcher_updates.cpp)
    target_link_libraries(launcher_update_tests PRIVATE beachlife_launcher_core)
    add_test(NAME launcher_updates
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/tests/launcher_update_fixture.py"
            "$<TARGET_FILE:launcher_update_tests>" "${CMAKE_CURRENT_BINARY_DIR}/launcher-update-tests")
endif()
set_property(TARGET beachlife APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_SOURCE_DIR}/assets/launcher/header.jpg"
    "${CMAKE_SOURCE_DIR}/assets/launcher/logo.png"
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
        "${CMAKE_SOURCE_DIR}/assets/launcher/logo.png"
        "${IMGUI_DIR}/misc/fonts/Roboto-Medium.ttf"
        "${BEACHLIFE_RESOURCE_DIR}"
    VERBATIM)
