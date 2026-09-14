find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)
include(ECMAddTests)

# Unit tests: pure logic, run on every build (ctest).
ecm_add_test(unit/mixerlogictest.cpp
    TEST_NAME mixerlogictest
    LINK_LIBRARIES kmixdeck_core Qt6::Test)

# Integration tests: private PipeWire daemon per session (pytest). Skipped if tools are missing.
find_program(PYTEST_EXECUTABLE NAMES pytest pytest-3 py.test)
find_program(PIPEWIRE_EXECUTABLE pipewire)
find_program(WIREPLUMBER_EXECUTABLE wireplumber)
find_program(FFMPEG_EXECUTABLE ffmpeg)
if(PYTEST_EXECUTABLE AND PIPEWIRE_EXECUTABLE AND WIREPLUMBER_EXECUTABLE AND FFMPEG_EXECUTABLE)
    add_test(NAME integration-audio-graph
        COMMAND ${PYTEST_EXECUTABLE} -q ${CMAKE_CURRENT_SOURCE_DIR}/integration
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/integration)
    set_tests_properties(integration-audio-graph PROPERTIES TIMEOUT 300 LABELS "integration")
else()
    message(STATUS "Integration tests disabled (need pytest, pipewire, wireplumber, ffmpeg)")
endif()
