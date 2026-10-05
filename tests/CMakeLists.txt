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
    set_tests_properties(frontend-qml-loads PROPERTIES LABELS "fast")
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
    set_tests_properties(frontend-qmllint PROPERTIES LABELS "fast")
    # qmllint exits 0 on warnings, so the gate is the message itself: these two are hard QML load errors.
    set_tests_properties(frontend-qmllint PROPERTIES TIMEOUT 120
        FAIL_REGULAR_EXPRESSION "Cannot assign;is not a type")
endif()

# VF-2: every ✅ requirement names an existing test (tools/sot-audit.py)
# Label "fast": alles, was KEINEN PipeWire-Sandkasten und keine Audiomessung braucht.
# Gemessen 2026-09-21: diese Gruppe laeuft in 17 s, die Audio-Integration in ~25 min.
# `ctest -L fast` ist das Gate fuer Text-, Doku- und CLI-Aenderungen; das Vollgate
# gehoert vor einen Commit, der Audioverhalten beruehrt — nicht hinter jede Zeile Kommentar.
set_tests_properties(mixerlogictest layouttest fxtest PROPERTIES LABELS "fast")

# Gate-Label: jeder Test gehoert in genau eins der beiden Gates. Der Filter selbst ist
# fail-open (fehlendes Label = stilles Ueberspringen), dieser Test macht ihn fail-closed.
add_test(NAME gate-label-vollstaendig COMMAND ${CMAKE_COMMAND}
    -DBUILD=${CMAKE_BINARY_DIR} -DCTEST=${CMAKE_CTEST_COMMAND}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/pruefe-label.cmake)
set_tests_properties(gate-label-vollstaendig PROPERTIES LABELS "fast")

# Schwester zum Label-Test: der prueft, dass jeder REGISTRIERTE Test in ein Gate faellt —
# dieser, dass jede Testdatei ueberhaupt registriert IST. Beides zusammen schliesst die
# Luecke "Datei existiert, ist gruen, laeuft aber in keinem Gate" (soundboard, 2026-09-22).
add_test(NAME suiten-vollstaendig
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/pruefe-suiten.py ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(suiten-vollstaendig PROPERTIES LABELS "fast")

add_test(NAME sot-audit COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/sot-audit.py)
set_tests_properties(sot-audit PROPERTIES LABELS "fast")

# Review v0.5: persistence through QSaveFile only (BP-7) and no internal references in shipped code.
# Measured red first: 2 bare QFile writers (scene save, UI export) + 4 person-name quotes in comments.
add_test(NAME hygiene COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/pruefe-hygiene.py)
set_tests_properties(hygiene PROPERTIES LABELS "fast")

# RQ-3: 100 % des D-Bus-Vertrags hat eine Beschreibung (tools/dbus-doc.py, exit 1 + Liste sonst).
# Das Werkzeug gab es, aber niemand rief es auf — gemessen 2026-09-22 waren 17 von 121 Mitgliedern
# undokumentiert (CT-8 8, CT-9 5, FX-9 4), und zwar seit Monaten still. Ein Werkzeug, das kein Test
# startet, ist keine Absicherung.
add_test(NAME dbus-doc-vollstaendig COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/dbus-doc.py)
set_tests_properties(dbus-doc-vollstaendig PROPERTIES LABELS "fast")

# CL-1: docs/kmixdeck.md is the ONE source for the man page, --help and docs/cli.md. These two tests keep it that
# way: `pruefen` rejects invented section names and any deviation from the man-pages(7) order, and groff -z parses
# the generated roff. Without the groff test, typography (—, →, Ü) silently drops out of the rendered page: 20 such
# warnings existed when the generator was first wired up on 2026-09-21 and none were visible in the markdown.
add_test(NAME doku-abschnitte COMMAND python3 ${CMAKE_SOURCE_DIR}/docs/generiere-doku.py pruefen)
set_tests_properties(doku-abschnitte PROPERTIES LABELS "fast")

# Review v0.4 (2026-09-22): jeder interne Doku-Link zeigt auf eine existierende Datei UND eine
# existierende Ueberschrift. README verwies auf `docs/cli.md#scenes` — Datei da, Abschnitt nicht
# (cli.md ist seit CL-1 nur ein Zeiger auf kmixdeck.md, der Anker wurde beim Umzug nicht
# mitgeprueft). Ein toter Anker springt im Browser stumm an den Dateianfang; gefunden wurde er
# nur durch Nachschlagen jedes einzelnen Links von Hand. Genau diese Handarbeit ersetzt der Test.
add_test(NAME doku-links COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/pruefe-doku-links.py ${CMAKE_SOURCE_DIR})
set_tests_properties(doku-links PROPERTIES LABELS "fast")

# CL-9: jedes Kommando der Dispatch-Tabelle steht in der Doku und umgekehrt.
# `scene` ging in v0.3.0 heraus, ohne im Hilfetext zu stehen — die Funktion war
# da, niemand konnte sie finden. Genau das faengt dieser Test.
add_test(NAME cl9-hilfe-gegen-code COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/pruefe-hilfe.py)
set_tests_properties(cl9-hilfe-gegen-code PROPERTIES LABELS "fast")

# CL-3: jedes Kommando hat eine eigene Hilfe mit mindestens einem Beispiel, und
# die Top-Level-Hilfe bleibt eine Schirmseite. Geprueft wird der GENERIERTE Text
# — ein Beispiel, das der Generator verschluckt, ist keins (loudness verlor sein
# Beispiel an einer Fortsetzungszeile, die mit ` anfing).
add_test(NAME cl3-kommando-hilfe COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/pruefe-kommando-hilfe.py)
set_tests_properties(cl3-kommando-hilfe PROPERTIES LABELS "fast")

# CL-2 + CL-4: Hilfe und Version ohne Daemon, ohne Bus-Aufruf, ohne argv[0] in
# der Usage-Zeile. Laeuft mit zerschossener DBUS_SESSION_BUS_ADDRESS.
add_test(NAME cl2-hilfe-ohne-daemon COMMAND ${CMAKE_COMMAND}
        -DCLI=$<TARGET_FILE:kmixdeck-cli>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/pruefe-hilfe-offline.cmake)
set_tests_properties(cl2-hilfe-ohne-daemon PROPERTIES LABELS "fast")
find_program(GROFF_EXECUTABLE groff)
if(GROFF_EXECUTABLE)
    add_test(NAME doku-groff COMMAND ${CMAKE_COMMAND}
            -DGENERATOR=${CMAKE_SOURCE_DIR}/docs/generiere-doku.py
            -DGROFF=${GROFF_EXECUTABLE}
            -DOUT=${CMAKE_CURRENT_BINARY_DIR}/kmixdeck-test.1
            -P ${CMAKE_CURRENT_SOURCE_DIR}/pruefe-manpage.cmake)
    set_tests_properties(doku-groff PROPERTIES LABELS "fast")
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
    # Sandbox-Leichen, die 8,5 h ueberlebten und die Grundlast dauerhaft auf 9-10 hielten).
    #
    # 2026-09-22 nachgemessen und damit geschlossen: test_ports.py ist KEIN Produktfehler, sondern reine
    # Ueberlastung. Im Gate (ctest -j, neun Audio-Suiten gleichzeitig) fiel die Suite nach 19,32 s; allein
    # gestartet bei Grundlast 1,51 laufen alle 20 Tests gruen in 348 s. Der Unterschied ist Faktor 18 in der
    # Laufzeit — die Suite kam im Gate gar nicht bis zu ihrer ersten Messung. Auch der zweite Gate-Rotlauf
    # dieser Runde (integration-service_cli, 21 von 44 rot mit `no session bus` und `pw-dump rc=255`) war
    # dasselbe Muster: er lief versehentlich parallel zum Gate. Konsequenz, keine Konfigaenderung: ein
    # Audio-Ergebnis zaehlt nur, wenn NICHTS anderes gleichzeitig einen PipeWire-Graphen haelt. Wer eine
    # Audio-Suite einzeln prueft, prueft vorher `pgrep -af gate.sh` und `/proc/loadavg`.
    # 🔴 Wer hier eine Suite vergisst, bekommt ein gruenes Gate ueber Tests, die nie liefen.
    # Am 2026-09-22 fehlte `soundboard` in dieser Liste: 10 CT-8-Tests, committet und als
    # fertig gemeldet, im Vollgate-Log 0 Treffer. Gefunden nur durch Abgleich `ls test_*.py`
    # gegen `ctest -N`. Genau das prueft jetzt der fast-Test `suiten-vollstaendig`.
    foreach(suite service_cli audio_graph lifecycle routing fx presentation layout ports frontends_sync soundboard streamdeck web)
        add_test(NAME integration-${suite}
            COMMAND ${PYTEST_EXECUTABLE} -q -p no:cacheprovider ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_${suite}.py
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/integration)
        set_tests_properties(integration-${suite} PROPERTIES TIMEOUT 600 LABELS "integration")
    endforeach()

    # CT-1 braucht zusaetzlich einen X-Server, XTEST und den echten Plasma-Shortcut-Daemon.
    # Die Suite drueckt eine Taste per XTEST (auf X-Protokollebene nicht von Hardware
    # unterscheidbar, xev meldet `synthetic NO`) und prueft am PipeWire-Knoten, dass gemutet
    # wurde — also die Kette X -> kglobalacceld -> kmixdeck-kde -> kmixdeckd. Fehlt eines der
    # Werkzeuge, ueberspringt sich die Datei selbst; darum hier nur der Pfad, keine Bedingung.
    find_program(XVFB_EXECUTABLE Xvfb)
    find_program(XDOTOOL_EXECUTABLE xdotool)
    if(XVFB_EXECUTABLE AND XDOTOOL_EXECUTABLE)
        add_test(NAME integration-shortcuts
            COMMAND ${PYTEST_EXECUTABLE} -q -p no:cacheprovider ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_shortcuts.py
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/integration)
        set_tests_properties(integration-shortcuts PROPERTIES TIMEOUT 600 LABELS "integration")
    else()
        message(STATUS "CT-1 shortcut test disabled (need Xvfb and xdotool)")
    endif()

    # CT-1 unter Wayland: dieselbe Kette, aber KWin statt X+kglobalacceld. Unter Wayland haelt
    # kwin_wayland selbst org.kde.kglobalaccel; die Taste kommt ueber KWins eigenes Protokoll fuer
    # synthetische Eingabe (org_kde_kwin_fake_input) und laeuft durch dieselbe Eingabeumleitung und
    # denselben Shortcut-Filter wie eine echte Tastatur. Warum nicht ydotool/wtype: KWin --virtual hat
    # kein libinput (uinput-Tasten kommen nie an), und KWin 6.6 bietet zwp_virtual_keyboard_v1 nicht an —
    # beides gemessen 2026-09-22. Der Helfer kwin-fake-key wird nur gebaut, nie installiert.
    find_program(KWIN_WAYLAND_EXECUTABLE kwin_wayland)
    find_program(WAYLAND_SCANNER_EXECUTABLE wayland-scanner)
    find_package(PlasmaWaylandProtocols QUIET)
    pkg_check_modules(WaylandClient QUIET IMPORTED_TARGET wayland-client)
    if(KWIN_WAYLAND_EXECUTABLE AND WAYLAND_SCANNER_EXECUTABLE AND PlasmaWaylandProtocols_FOUND AND WaylandClient_FOUND)
        enable_language(C)   # wayland-scanner erzeugt C; das Hauptprojekt bleibt CXX-only
        set(_fake_xml ${PLASMA_WAYLAND_PROTOCOLS_DIR}/fake-input.xml)
        set(_fake_h ${CMAKE_CURRENT_BINARY_DIR}/fake-input-client-protocol.h)
        set(_fake_c ${CMAKE_CURRENT_BINARY_DIR}/fake-input-protocol.c)
        add_custom_command(OUTPUT ${_fake_h} ${_fake_c}
            COMMAND ${WAYLAND_SCANNER_EXECUTABLE} client-header ${_fake_xml} ${_fake_h}
            COMMAND ${WAYLAND_SCANNER_EXECUTABLE} private-code ${_fake_xml} ${_fake_c}
            DEPENDS ${_fake_xml} VERBATIM)
        add_executable(kwin-fake-key tools/kwin-fake-key.c ${_fake_c} ${_fake_h})
        set_source_files_properties(${_fake_c} PROPERTIES LANGUAGE C)
        target_include_directories(kwin-fake-key PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        target_link_libraries(kwin-fake-key PRIVATE PkgConfig::WaylandClient)
        set_target_properties(kwin-fake-key PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
                                                       LINKER_LANGUAGE C)
        add_test(NAME integration-shortcuts_wayland
            COMMAND ${PYTEST_EXECUTABLE} -q -p no:cacheprovider ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_shortcuts_wayland.py
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/integration)
        set_tests_properties(integration-shortcuts_wayland PROPERTIES TIMEOUT 600 LABELS "integration")
    else()
        message(STATUS "CT-1 Wayland shortcut test disabled (need kwin_wayland, wayland-scanner, "
                       "plasma-wayland-protocols, wayland-client)")
    endif()

    # UX-17 / ADR 0014 HY-2: the tray popover under Wayland is a layer surface. Without LayerShellQt the frontend
    # opens the window instead, by design, so the test only exists where the build has the layer-shell path.
    if(KWIN_WAYLAND_EXECUTABLE AND LayerShellQt_FOUND)
        add_test(NAME integration-tray_wayland
            COMMAND ${PYTEST_EXECUTABLE} -q -p no:cacheprovider ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_tray_wayland.py
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/integration)
        set_tests_properties(integration-tray_wayland PROPERTIES TIMEOUT 300 LABELS "integration")
    else()
        message(STATUS "UX-17 Wayland tray test disabled (need kwin_wayland and LayerShellQt)")
    endif()

    # ADR 0014 HY-1: shortcuts through the GlobalShortcuts portal in a nested Hyprland (tools/hyprland-sandbox.py).
    # The test skips itself when Hyprland, XDPH, Waybar, mako or wtype are missing (Ubuntu CI has no Hyprland).
    find_program(HYPRLAND_EXECUTABLE Hyprland)
    if(HYPRLAND_EXECUTABLE AND KWIN_WAYLAND_EXECUTABLE)
        add_test(NAME integration-shortcuts_hyprland
            COMMAND ${PYTEST_EXECUTABLE} -q -p no:cacheprovider ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_shortcuts_hyprland.py
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/integration)
        set_tests_properties(integration-shortcuts_hyprland PROPERTIES TIMEOUT 300 LABELS "integration")
    else()
        message(STATUS "HY-1 Hyprland shortcut test disabled (need Hyprland and kwin_wayland as its host)")
    endif()
else()
    message(STATUS "Integration tests disabled (need pytest, pipewire, wireplumber, ffmpeg)")
endif()
