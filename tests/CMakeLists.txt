find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)
include(ECMAddTests)

# Unit tests: pure logic, run on every build (ctest).
ecm_add_test(unit/mixerlogictest.cpp
    TEST_NAME mixerlogictest
    LINK_LIBRARIES kmixdeck_core Qt6::Test)
# UT-1/UT-2: persistence format round trip, legacy/partial/newer documents, DeviceRef grammar, golden PipeWire conf.
ecm_add_test(unit/layouttest.cpp
    TEST_NAME layouttest
    LINK_LIBRARIES kmixdeck_core Qt6::Test)
# UT-3: fx catalog, validation, rendered filter-chain args, live-control names — no LADSPA library needed.
ecm_add_test(unit/fxtest.cpp
    TEST_NAME fxtest
    LINK_LIBRARIES kmixdeck_core Qt6::Test)

# Frontend smoke: every QML file must load (offscreen, no daemon needed — the client shows "not reachable").
if(BUILD_KDE_FRONTEND)
    add_test(NAME frontend-qml-loads COMMAND kmixdeck-kde --self-test)
    set_tests_properties(frontend-qml-loads PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_LOGGING_RULES=*.debug=false" TIMEOUT 60 FAIL_REGULAR_EXPRESSION "failed to load component;is not a type;unavailable")
endif()
# qmllint as a pre-flight: --self-test needs a full build + 5s of runtime, qmllint finds the same class of
# mistake (a non-Action child in a Kirigami.Action, an incompatible default property) in ~2s WITHOUT running.
# Anlass 2026-09-19: one such child in Main.qml stopped the whole QML load, and the failure surfaced as ten
# unrelated red frontend tests (KeyError on every probe) instead of one clear message.
find_program(QMLLINT_EXECUTABLE NAMES qmllint qmllint-qt6 HINTS /usr/lib/qt6/bin /usr/lib64/qt6/bin)
if(QMLLINT_EXECUTABLE AND BUILD_KDE_FRONTEND)
    file(GLOB KMIXDECK_QML_FILES ${CMAKE_SOURCE_DIR}/src/qml/*.qml)
    add_test(NAME frontend-qmllint COMMAND ${QMLLINT_EXECUTABLE} ${KMIXDECK_QML_FILES})
    # qmllint exits 0 on warnings, so the gate is the message itself: these two are hard QML load errors.
    set_tests_properties(frontend-qmllint PROPERTIES TIMEOUT 120
        FAIL_REGULAR_EXPRESSION "Cannot assign;is not a type")
endif()

# VF-2: every ✅ requirement names an existing test (tools/sot-audit.py)
add_test(NAME sot-audit COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/sot-audit.py)

# Integration tests: private PipeWire daemon per session (pytest). Skipped if tools are missing.
find_program(PYTEST_EXECUTABLE NAMES pytest pytest-3 py.test)
find_program(PIPEWIRE_EXECUTABLE pipewire)
find_program(WIREPLUMBER_EXECUTABLE wireplumber)
find_program(FFMPEG_EXECUTABLE ffmpeg)
if(PYTEST_EXECUTABLE AND PIPEWIRE_EXECUTABLE AND WIREPLUMBER_EXECUTABLE AND FFMPEG_EXECUTABLE)
    # One ctest per file so they run in parallel (ctest -j) and a failure names the area, not "everything".
    foreach(suite service_cli audio_graph lifecycle routing fx presentation ports frontends_sync streamdeck web)
        add_test(NAME integration-${suite}
            COMMAND ${PYTEST_EXECUTABLE} -q -p no:cacheprovider ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_${suite}.py
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/integration)
        set_tests_properties(integration-${suite} PROPERTIES TIMEOUT 600 LABELS "integration")
    endforeach()
else()
    message(STATUS "Integration tests disabled (need pytest, pipewire, wireplumber, ffmpeg)")
endif()
