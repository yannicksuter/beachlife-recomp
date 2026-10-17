cmake_minimum_required(VERSION 3.25)
project(beachlife_recomp VERSION 0.0.1 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build: cmake --preset debug")
endif()

find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
find_package(Git REQUIRED)
include(CTest)

set(BEACHLIFE_BUILD_CHANNEL "dev" CACHE STRING "Build origin: dev, test or release")
set_property(CACHE BEACHLIFE_BUILD_CHANNEL PROPERTY STRINGS dev test release)
set(BEACHLIFE_BUILD_PULL_REQUEST "0" CACHE STRING "Source pull request number")
set(BEACHLIFE_BUILD_RUN "0" CACHE STRING "GitHub Actions run ID")

if(WIN32)
    set(BEACHLIFE_SYSTEM windows)
elseif(APPLE)
    set(BEACHLIFE_SYSTEM macos)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(BEACHLIFE_SYSTEM linux)
else()
    message(FATAL_ERROR "Unsupported system: ${CMAKE_SYSTEM_NAME}")
endif()
set(BEACHLIFE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    set(BEACHLIFE_ARCH "${CMAKE_OSX_ARCHITECTURES}")
endif()
string(TOLOWER "${BEACHLIFE_ARCH}" BEACHLIFE_ARCH)
if(BEACHLIFE_ARCH MATCHES "^(amd64|x86_64)$")
    set(BEACHLIFE_ARCH x86_64)
elseif(BEACHLIFE_ARCH MATCHES "^(aarch64|arm64)$")
    set(BEACHLIFE_ARCH arm64)
else()
    message(FATAL_ERROR "Unsupported architecture: ${BEACHLIFE_ARCH}")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The native host requires a 64-bit compiler")
endif()
set(BEACHLIFE_PACKAGE_PLATFORM "${BEACHLIFE_SYSTEM}" CACHE STRING "Package platform (or steamdeck on Linux)")
if(NOT BEACHLIFE_PACKAGE_PLATFORM STREQUAL BEACHLIFE_SYSTEM)
    if(NOT (BEACHLIFE_SYSTEM STREQUAL "linux" AND BEACHLIFE_ARCH STREQUAL "x86_64"
            AND BEACHLIFE_PACKAGE_PLATFORM STREQUAL "steamdeck"))
        message(FATAL_ERROR "Invalid package platform for this target")
    endif()
endif()

set(BEACHLIFE_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(cmake/build-config.json.in "${BEACHLIFE_GENERATED}/build-config.json" @ONLY)
add_custom_target(build_identity
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_version.py"
        --source "${CMAKE_CURRENT_SOURCE_DIR}"
        --config "${BEACHLIFE_GENERATED}/build-config.json"
        --output "${BEACHLIFE_GENERATED}"
        --git "${GIT_EXECUTABLE}"
    BYPRODUCTS "${BEACHLIFE_GENERATED}/build_version.h" "${BEACHLIFE_GENERATED}/build-info.json"
    COMMENT "Record the CMake version and current source revision"
    VERBATIM)

add_library(beachlife_formats STATIC src/platform/json.cpp src/platform/sha256.cpp)
target_include_directories(beachlife_formats PUBLIC src)
target_compile_features(beachlife_formats PUBLIC cxx_std_20)
set_target_properties(beachlife_formats PROPERTIES CXX_EXTENSIONS OFF)

add_library(beachlife_data STATIC src/data/iso9660.cpp src/data/game_path.cpp src/data/source.cpp
    src/data/folder_source.cpp src/data/local_files.cpp)
target_include_directories(beachlife_data PUBLIC src)
target_compile_features(beachlife_data PUBLIC cxx_std_20)
set_target_properties(beachlife_data PROPERTIES CXX_EXTENSIONS OFF)

add_executable(beachlife-data src/data/main.cpp)
target_link_libraries(beachlife-data PRIVATE beachlife_data)
set_target_properties(beachlife-data PROPERTIES CXX_EXTENSIONS OFF)
if(MINGW)
    target_link_options(beachlife-data PRIVATE -municode)
endif()

add_executable(beachlife src/bootstrap/main.cpp)
target_link_libraries(beachlife PRIVATE beachlife_data)
target_compile_features(beachlife PRIVATE cxx_std_20)
set_target_properties(beachlife PROPERTIES CXX_EXTENSIONS OFF)
if(WIN32)
    set_target_properties(beachlife PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
target_include_directories(beachlife PRIVATE "${BEACHLIFE_GENERATED}")
add_dependencies(beachlife build_identity)
foreach(target beachlife beachlife-data beachlife_data beachlife_formats)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endforeach()
if(APPLE)
    set_target_properties(beachlife PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Beach Life"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.beachlife.recomp"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
        OUTPUT_NAME beachlife)
endif()

include(cmake/Launcher.cmake)
include(cmake/Cabinet.cmake)
include(cmake/Matching.cmake)

if(BUILD_TESTING)
    add_test(NAME bootstrap_cli
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_cli.py"
            "$<TARGET_FILE:beachlife>" "${BEACHLIFE_GENERATED}/build-info.json")
    add_test(NAME build_tools
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover -s tests -p "test_*.py")
    set_tests_properties(build_tools PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_executable(launcher_settings_tests tests/launcher_settings.cpp src/launcher/settings.cpp)
    target_include_directories(launcher_settings_tests PRIVATE src)
    target_compile_features(launcher_settings_tests PRIVATE cxx_std_20)
    add_test(NAME launcher_settings COMMAND launcher_settings_tests)
    add_test(NAME launcher_window
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/check_launcher.py"
            "$<TARGET_FILE:beachlife>")
    add_test(NAME iso_directory_index
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/check_iso.py"
            "$<TARGET_FILE:beachlife>")
    add_executable(game_path_tests tests/game_path.cpp src/data/game_path.cpp)
    target_include_directories(game_path_tests PRIVATE src)
    target_compile_features(game_path_tests PRIVATE cxx_std_20)
    add_test(NAME game_paths COMMAND game_path_tests)
    add_executable(iso_source_tests tests/iso_source.cpp)
    target_link_libraries(iso_source_tests PRIVATE beachlife_data)
    add_test(NAME iso_file_reads
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/check_data.py"
            "$<TARGET_FILE:beachlife-data>" "$<TARGET_FILE:iso_source_tests>")
    add_test(NAME cabinet_index
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/check_cabinet.py"
            "$<TARGET_FILE:beachlife-data>")
    add_executable(folder_source_tests tests/folder_source.cpp)
    target_link_libraries(folder_source_tests PRIVATE beachlife_data)
    add_test(NAME prepared_folder_reads
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/check_folder.py"
            "$<TARGET_FILE:folder_source_tests>")
    add_executable(game_index_tests tests/game_index.cpp)
    target_link_libraries(game_index_tests PRIVATE beachlife_cabinet)
    add_test(NAME game_content_paths COMMAND game_index_tests)
    add_test(NAME game_content_reads
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/check_game_content.py"
            "$<TARGET_FILE:beachlife-data>")
    add_executable(content_manifest_tests tests/content_manifest.cpp)
    target_link_libraries(content_manifest_tests PRIVATE beachlife_cabinet)
    add_test(NAME content_manifests COMMAND content_manifest_tests)
    add_executable(content_prepare_tests tests/content_prepare.cpp)
    target_link_libraries(content_prepare_tests PRIVATE beachlife_cabinet)
    add_test(NAME prepared_content
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/check_vfs.py"
            "$<TARGET_FILE:beachlife-data>" "$<TARGET_FILE:content_prepare_tests>")
endif()
