# CL-2 + CL-4: help and version must work without a daemon, without touching the bus, and the usage line must
# name the tool rather than argv[0].
#
# Why with a broken DBUS_SESSION_BUS_ADDRESS and not just "without a running daemon": a wrong address is the
# harsher test. If any code path tries a bus call, it fails loudly instead of quietly autostarting the service —
# D-Bus activation would otherwise mask exactly the bug this test is about.
#
# CL-4 measured before the fix (2026-09-21): `Usage: /var/tmp/build_cl1/bin/kmixdeck [options] command`.
# QCommandLineParser::showHelp() builds that line from argv[0], which is why the CLI prints the help itself.

if(NOT CLI)
    message(FATAL_ERROR "call with -DCLI=<path to the kmixdeck binary>")
endif()

set(KAPUTTER_BUS "unix:path=/nonexistent/kmixdeck-cl2-test")

foreach(argument "--help" "-h" "help" "--version")
    execute_process(COMMAND ${CMAKE_COMMAND} -E env DBUS_SESSION_BUS_ADDRESS=${KAPUTTER_BUS} ${CLI} ${argument}
                    OUTPUT_VARIABLE ausgabe
                    ERROR_VARIABLE fehler
                    RESULT_VARIABLE rc
                    TIMEOUT 10)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "`kmixdeck ${argument}` exited ${rc} without a bus — CL-2 says it must work offline.\n"
                            "stderr: ${fehler}")
    endif()
    if(NOT fehler STREQUAL "")
        message(FATAL_ERROR "`kmixdeck ${argument}` wrote to stderr without a bus (CL-2: no bus call at all):\n${fehler}")
    endif()
    # Mindestlaenge je Zweck: --version ist EIN Zeile ("kmixdeck 0.3.0", 15
    # Zeichen) und soll das auch bleiben; die Hilfe ist eine Schirmseite. Eine
    # gemeinsame Schwelle von 40 Zeichen liess den Test an der korrekten
    # Version-Ausgabe scheitern (2026-09-21) — der Test war falsch, nicht das
    # Programm.
    if(argument STREQUAL "--version")
        if(NOT ausgabe MATCHES "^kmixdeck [0-9]+\\.[0-9]+")
            message(FATAL_ERROR "`kmixdeck --version` must print `kmixdeck <version>`, got: ${ausgabe}")
        endif()
    else()
        string(LENGTH "${ausgabe}" laenge)
        if(laenge LESS 400)
            message(FATAL_ERROR "`kmixdeck ${argument}` printed only ${laenge} characters — that is not a help "
                                "text:\n${ausgabe}")
        endif()
    endif()

    # CL-4: no path anywhere in the output, and no Qt-generated "Usage: <path>" line.
    if(ausgabe MATCHES "Usage:[^\n]*/")
        string(REGEX MATCH "Usage:[^\n]*" zeile "${ausgabe}")
        message(FATAL_ERROR "CL-4 violated by `kmixdeck ${argument}`: the usage line carries a path instead of the "
                            "tool name — ${zeile}")
    endif()
endforeach()

# The help must actually list the commands, otherwise it is not help (CL-3).
execute_process(COMMAND ${CMAKE_COMMAND} -E env DBUS_SESSION_BUS_ADDRESS=${KAPUTTER_BUS} ${CLI} --help
                OUTPUT_VARIABLE hilfe TIMEOUT 10)
foreach(kommando status mix cell channel scene fx app devices)
    if(NOT hilfe MATCHES "[ \n]${kommando}[ \n]")
        message(FATAL_ERROR "`kmixdeck --help` does not mention the command `${kommando}` (CL-3/CL-9).")
    endif()
endforeach()

# CL-3: per-command help must work the same way offline, for both spellings, and every command must carry an
# example. Measured 2026-09-21: all 18 commands reachable, 28 examples in total.
foreach(kommando status levels loudness watch undo setup export import channel mix cell app devices listen
        audition fx scene streamdeck)
    foreach(schreibweise "help;${kommando}" "${kommando};--help")
        execute_process(COMMAND ${CMAKE_COMMAND} -E env DBUS_SESSION_BUS_ADDRESS=${KAPUTTER_BUS}
                                ${CLI} ${schreibweise}
                        OUTPUT_VARIABLE hilfe_k ERROR_VARIABLE fehler_k RESULT_VARIABLE rc_k TIMEOUT 10)
        if(NOT rc_k EQUAL 0)
            message(FATAL_ERROR "`kmixdeck ${schreibweise}` exited ${rc_k} offline (CL-2/CL-3): ${fehler_k}")
        endif()
        if(NOT hilfe_k MATCHES "kmixdeck ${kommando}")
            message(FATAL_ERROR "`kmixdeck ${schreibweise}` does not show help for `${kommando}` (CL-3):\n${hilfe_k}")
        endif()
        if(NOT hilfe_k MATCHES "\\$ kmixdeck")
            message(FATAL_ERROR "the help for `${kommando}` has no worked example (CL-3 requires at least one) — "
                                "add a '> kmixdeck …' line in docs/kmixdeck.md")
        endif()
    endforeach()
endforeach()

# A typo must be a usage error, not a silent full help: otherwise the user hunts their mistake in 54 lines.
execute_process(COMMAND ${CMAKE_COMMAND} -E env DBUS_SESSION_BUS_ADDRESS=${KAPUTTER_BUS} ${CLI} help mixx
                OUTPUT_VARIABLE tippfehler_out ERROR_VARIABLE tippfehler_err RESULT_VARIABLE rc_t TIMEOUT 10)
if(rc_t EQUAL 0)
    message(FATAL_ERROR "`kmixdeck help mixx` succeeded — an unknown command must exit non-zero (CL-8).")
endif()
if(NOT tippfehler_err MATCHES "mixx")
    message(FATAL_ERROR "the error for an unknown command does not name it (CL-8):\n${tippfehler_err}")
endif()

message(STATUS "CL-2/CL-3/CL-4 ok: help and version work with no bus, 18 commands with examples, usage line names "
               "the tool")
