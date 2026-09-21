# CL-1: run docs/generiere-doku.py and write its stdout to a file.
#
# Why this file exists: add_custom_command has no shell, so neither `> out` nor OUTPUT_FILE works there — both are
# passed to the script as literal arguments. Verified 2026-09-21: the `manpage` target reported "Built target
# manpage" and produced no file at all. execute_process DOES have OUTPUT_FILE, so the redirect lives here.
#
# ZIEL selects what to generate: `man` (the man page) or `header` (hilfe_text.h with the --help text for the CLI).

if(NOT PYTHON3 OR NOT GENERATOR OR NOT OUT)
    message(FATAL_ERROR "call with -DPYTHON3=... -DGENERATOR=... -DOUT=... [-DZIEL=man|header] [-DVERSION=...]")
endif()
if(NOT ZIEL)
    set(ZIEL man)
endif()

execute_process(COMMAND ${CMAKE_COMMAND} -E env KMIXDECK_VERSION=${VERSION}
                        ${PYTHON3} ${GENERATOR} ${ZIEL}
                OUTPUT_FILE ${OUT}
                ERROR_VARIABLE err
                RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    file(REMOVE ${OUT})   # no half-written output in the build tree
    message(FATAL_ERROR "generiere-doku.py ${ZIEL} failed (${rc}): ${err}")
endif()

# An empty file would install / compile silently: `man kmixdeck` would show nothing, `--help` would print nothing.
file(SIZE ${OUT} groesse)
if(groesse LESS 500)
    message(FATAL_ERROR "generated ${ZIEL} is suspiciously small (${groesse} bytes): ${OUT}")
endif()
