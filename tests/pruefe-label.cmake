# SPDX-License-Identifier: GPL-3.0-or-later
#
# Jeder Test MUSS genau ein Gate-Label tragen: "fast" oder "integration".
#
# 🔴 Warum es diesen Test gibt (gemessen 2026-09-21): das Schnellgate wurde als
# `ctest -L fast` eingefuehrt, und drei von 22 Tests trugen kein Label —
# appstreamtest, frontend-qml-loads, frontend-qmllint. Die liefen danach in
# KEINEM Gate mehr: nicht im schnellen (kein Label "fast") und nicht im
# Vollgate-Filter (kein Label "integration"). Zwei davon waren echte
# Prueftests der KDE-Oberflaeche.
#
# Das ist die Eigenschaft, die einen Label-Filter als Gate gefaehrlich macht:
# er ist fail-OPEN. Ein vergessenes Label fuehrt nicht zu einem Fehler, sondern
# zu stillem Ueberspringen — der Lauf bleibt gruen und deckt weniger ab. Ein
# neuer Test ohne Label faellt niemandem auf, bis etwas durchrutscht.
#
# Darum dreht dieser Test die Richtung um: fail-CLOSED. Wer einen Test ohne
# Label anlegt, bekommt einen roten Lauf mit dem Namen des Tests.
#
# Aufruf: cmake -DBUILD=<build-dir> -DCTEST=<ctest> -P pruefe-label.cmake

if(NOT DEFINED BUILD OR NOT DEFINED CTEST)
    message(FATAL_ERROR "BUILD und CTEST muessen gesetzt sein")
endif()

# Alle Tests, und die beiden Label-Mengen. `-N` listet nur, ohne auszufuehren.
execute_process(COMMAND ${CTEST} --test-dir ${BUILD} -N
                OUTPUT_VARIABLE alle_rohr ERROR_VARIABLE fehler RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "ctest -N fehlgeschlagen: ${fehler}")
endif()

function(namen_einsammeln rohr ausgabe)
    set(gefunden "")
    string(REPLACE "\n" ";" zeilen "${rohr}")
    foreach(zeile IN LISTS zeilen)
        # Format: "  Test  #3: layouttest"
        if(zeile MATCHES "Test +#[0-9]+: +([^ ]+)")
            list(APPEND gefunden "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    set(${ausgabe} "${gefunden}" PARENT_SCOPE)
endfunction()

namen_einsammeln("${alle_rohr}" alle)

set(beschriftet "")
foreach(label fast integration)
    execute_process(COMMAND ${CTEST} --test-dir ${BUILD} -N -L ${label}
                    OUTPUT_VARIABLE rohr RESULT_VARIABLE rc2)
    if(NOT rc2 EQUAL 0)
        message(FATAL_ERROR "ctest -N -L ${label} fehlgeschlagen")
    endif()
    namen_einsammeln("${rohr}" treffer)
    list(APPEND beschriftet ${treffer})
    list(LENGTH treffer anzahl)
    message(STATUS "  Label ${label}: ${anzahl} Tests")
endforeach()

list(LENGTH alle gesamt)
if(gesamt EQUAL 0)
    message(FATAL_ERROR "ctest hat keine Tests gemeldet — Build-Verzeichnis falsch?")
endif()

set(ohne_label "")
foreach(name IN LISTS alle)
    if(NOT name IN_LIST beschriftet)
        list(APPEND ohne_label "${name}")
    endif()
endforeach()

if(ohne_label)
    list(LENGTH ohne_label wieviele)
    string(REPLACE ";" "\n  " liste "${ohne_label}")
    message(FATAL_ERROR
        "${wieviele} von ${gesamt} Tests tragen kein Gate-Label und laufen damit in KEINEM Gate:\n"
        "  ${liste}\n"
        "\n"
        "Jeder Test braucht LABELS \"fast\" ODER LABELS \"integration\":\n"
        "  fast        = kein PipeWire-Sandkasten, keine Audiomessung (Sekunden)\n"
        "  integration = eigener Sandkasten, Audio wird gemessen (Minuten)\n"
        "\n"
        "Beispiel:\n"
        "  set_tests_properties(<name> PROPERTIES LABELS \"fast\")")
endif()

# Doppelt beschriftete Tests: dann ist die Gate-Zuordnung mehrdeutig.
set(doppelt "")
foreach(name IN LISTS alle)
    set(zaehler 0)
    foreach(eintrag IN LISTS beschriftet)
        if(eintrag STREQUAL name)
            math(EXPR zaehler "${zaehler} + 1")
        endif()
    endforeach()
    if(zaehler GREATER 1)
        list(APPEND doppelt "${name}")
    endif()
endforeach()
if(doppelt)
    string(REPLACE ";" ", " liste "${doppelt}")
    message(FATAL_ERROR "Diese Tests tragen BEIDE Gate-Label, die Zuordnung ist mehrdeutig: ${liste}")
endif()

message(STATUS "Label-Pruefung ok: alle ${gesamt} Tests tragen genau ein Gate-Label")
