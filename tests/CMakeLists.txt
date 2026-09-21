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
# Label "schnell": alles, was KEINEN PipeWire-Sandkasten und keine Audiomessung braucht.
# Gemessen 2026-09-21: diese Gruppe laeuft in 17 s, die Audio-Integration in ~25 min.
# `ctest -L schnell` ist das Gate fuer Text-, Doku- und CLI-Aenderungen; das Vollgate
# gehoert vor einen Commit, der Audioverhalten beruehrt — nicht hinter jede Zeile Kommentar.
set_tests_properties(mixerlogictest layouttest fxtest PROPERTIES LABELS "schnell")

add_test(NAME sot-audit COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/sot-audit.py)
set_tests_properties(sot-audit PROPERTIES LABELS "schnell")

# CL-1: docs/kmixdeck.md is the ONE source for the man page, --help and docs/cli.md. These two tests keep it that
# way: `pruefen` rejects invented section names and any deviation from the man-pages(7) order, and groff -z parses
# the generated roff. Without the groff test, typography (—, →, Ü) silently drops out of the rendered page: 20 such
# warnings existed when the generator was first wired up on 2026-09-21 and none were visible in the markdown.
add_test(NAME doku-abschnitte COMMAND python3 ${CMAKE_SOURCE_DIR}/docs/generiere-doku.py pruefen)
set_tests_properties(doku-abschnitte PROPERTIES LABELS "schnell")

# CL-9: jedes Kommando der Dispatch-Tabelle steht in der Doku und umgekehrt.
# `scene` ging in v0.3.0 heraus, ohne im Hilfetext zu stehen — die Funktion war
# da, niemand konnte sie finden. Genau das faengt dieser Test.
add_test(NAME cl9-hilfe-gegen-code COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/pruefe-hilfe.py)
set_tests_properties(cl9-hilfe-gegen-code PROPERTIES LABELS "schnell")

# CL-3: jedes Kommando hat eine eigene Hilfe mit mindestens einem Beispiel, und
# die Top-Level-Hilfe bleibt eine Schirmseite. Geprueft wird der GENERIERTE Text
# — ein Beispiel, das der Generator verschluckt, ist keins (loudness verlor sein
# Beispiel an einer Fortsetzungszeile, die mit ` anfing).
add_test(NAME cl3-kommando-hilfe COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/pruefe-kommando-hilfe.py)
set_tests_properties(cl3-kommando-hilfe PROPERTIES LABELS "schnell")

# CL-2 + CL-4: Hilfe und Version ohne Daemon, ohne Bus-Aufruf, ohne argv[0] in
# der Usage-Zeile. Laeuft mit zerschossener DBUS_SESSION_BUS_ADDRESS.
add_test(NAME cl2-hilfe-ohne-daemon COMMAND ${CMAKE_COMMAND}
        -DCLI=$<TARGET_FILE:kmixdeck-cli>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/pruefe-hilfe-offline.cmake)
set_tests_properties(cl2-hilfe-ohne-daemon PROPERTIES LABELS "schnell")
find_program(GROFF_EXECUTABLE groff)
if(GROFF_EXECUTABLE)
    add_test(NAME doku-groff COMMAND ${CMAKE_COMMAND}
            -DGENERATOR=${CMAKE_SOURCE_DIR}/docs/generiere-doku.py
            -DGROFF=${GROFF_EXECUTABLE}
            -DOUT=${CMAKE_CURRENT_BINARY_DIR}/kmixdeck-test.1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/pruefe-manpage.cmake)
    set_tests_properties(doku-groff PROPERTIES LABELS "schnell")
endif()

# Integration tests: private PipeWire daemon per session (pytest). Skipped if tools are missing.
find_program(PYTEST_EXECUTABLE NAMES pytest pytest-3 py.test)
find_program(PIPEWIRE_EXECUTABLE pipewire)
find_program(WIREPLUMBER_EXECUTABLE wireplumber)
find_program(FFMPEG_EXECUTABLE ffmpeg)
if(PYTEST_EXECUTABLE AND PIPEWIRE_EXECUTABLE AND WIREPLUMBER_EXECUTABLE AND FFMPEG_EXECUTABLE)
    # One ctest per file so they run in parallel (ctest -j) and a failure names the area, not "everything".
    #
    # One ctest per file so they run in parallel (ctest -j) and a failure names the area, not "everything".
    #
    # 🔴 KEIN RESOURCE_LOCK. Am 2026-09-21 habe ich einen eingebaut und wieder entfernt — beides gemessen:
    #
    # Die Vermutung war, dass `-j2` zwei soft-realtime-PipeWire-Graphen auf 4 Kerne presst und die
    # Audiomessungen deshalb `-inf dB` liefern. Der Lock serialisiert die neun Audio-Suiten; Kosten: die
    # Suiten brauchen zusammen ~25 min, seriell statt parallel verdoppelt das die Gate-Laufzeit auf ~52 min
    # fuer zwei Durchlaeufe. Gemessen wurde dann mit Lock UND auf aufgeraeumter Maschine (Grundlast 4,19) —
    # und test_ports.py fiel TROTZDEM mit `last reading -inf dB` aus. Der Lock hat den Fehler also nicht
    # beseitigt, nur den Gate-Lauf verlangsamt: Symptom-Doktern mit Rechnung.
    #
    # Die echte Sanierung dieser Runde steckt in integration/conftest.py (PR_SET_PDEATHSIG gegen
    # Sandbox-Leichen, die 8,5 h ueberlebten und die Grundlast dauerhaft auf 9-10 hielten). Der Restfehler in
    # test_ports.py ist damit NICHT erklaert und bleibt offen — er gehoert diagnostiziert, nicht wegserialisiert.
    foreach(suite service_cli audio_graph lifecycle routing fx presentation ports frontends_sync streamdeck web)
        add_test(NAME integration-${suite}
            COMMAND ${PYTEST_EXECUTABLE} -q -p no:cacheprovider ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_${suite}.py
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/integration)
        set_tests_properties(integration-${suite} PROPERTIES TIMEOUT 600 LABELS "integration")
    endforeach()
else()
    message(STATUS "Integration tests disabled (need pytest, pipewire, wireplumber, ffmpeg)")
endif()
