set(BEACHLIFE_MATCHING_COMPILER "" CACHE PATH "Local Visual C++ 6.0 SP4 directory")
set(BEACHLIFE_RECCMP "reccmp-reccmp" CACHE FILEPATH "Pinned reccmp comparison command")
set(BEACHLIFE_MATCHING_ORIGINAL "${CMAKE_SOURCE_DIR}/orig/analysis/disc/BeachLife.exe"
    CACHE FILEPATH "Verified original executable for local comparisons")
add_custom_target(matching_fixture
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/matching.py" fixture
        --compiler "${BEACHLIFE_MATCHING_COMPILER}"
        --reccmp "${BEACHLIFE_RECCMP}"
        --output "${CMAKE_BINARY_DIR}/matching"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Verify the original-toolchain comparison fixture"
    VERBATIM)
add_custom_target(matching_stdio_fixture
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/matching.py" stdio-fixture
        --compiler "${BEACHLIFE_MATCHING_COMPILER}"
        --reccmp "${BEACHLIFE_RECCMP}"
        --output "${CMAKE_BINARY_DIR}/matching"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Verify generated VC6 stdio import linkage"
    VERBATIM)
add_custom_target(matching_game
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/matching.py" game
        --compiler "${BEACHLIFE_MATCHING_COMPILER}"
        --reccmp "${BEACHLIFE_RECCMP}"
        --original "${BEACHLIFE_MATCHING_ORIGINAL}"
        --output "${CMAKE_BINARY_DIR}/matching"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Compare annotated reconstructed functions with the verified target"
    VERBATIM)
