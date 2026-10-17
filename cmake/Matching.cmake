set(BEACHLIFE_MATCHING_COMPILER "" CACHE PATH "Local Visual C++ 6.0 SP4 directory")
set(BEACHLIFE_RECCMP "reccmp-reccmp" CACHE FILEPATH "Pinned reccmp comparison command")
add_custom_target(matching_fixture
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/matching.py" fixture
        --compiler "${BEACHLIFE_MATCHING_COMPILER}"
        --reccmp "${BEACHLIFE_RECCMP}"
        --output "${CMAKE_BINARY_DIR}/matching"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Verify the original-toolchain comparison fixture"
    VERBATIM)
