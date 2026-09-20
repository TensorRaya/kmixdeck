# CL-1: generate kmixdeck.1 and let groff parse it. Fails on ANY groff warning.
#
# Why a -P script and not a plain add_test: the test needs a pipe (generator -> file -> groff) and add_test has no
# shell. `-ww -z` means "all warnings, parse only, no output" — the exit code alone is not enough, groff reports
# unknown input characters as warnings and still exits 0, so the stderr text is what decides here.
#
# What this catches (measured 2026-09-21, first wiring of the generator): 20 "invalid input character code"
# warnings for —, →, ←, −, Ü, ï. None of them are visible in the markdown source, and in the rendered page the
# characters were simply gone. A man page is not a file you eyeball once; this test is the only thing that keeps
# typography from rotting away silently.

if(NOT GENERATOR OR NOT GROFF OR NOT OUT)
    message(FATAL_ERROR "call with -DGENERATOR=... -DGROFF=... -DOUT=...")
endif()

find_program(PYTHON3 NAMES python3 REQUIRED)

execute_process(COMMAND ${PYTHON3} ${GENERATOR} man
                OUTPUT_FILE ${OUT}
                ERROR_VARIABLE gen_err
                RESULT_VARIABLE gen_rc)
if(NOT gen_rc EQUAL 0)
    message(FATAL_ERROR "generiere-doku.py man failed (${gen_rc}): ${gen_err}")
endif()

execute_process(COMMAND ${GROFF} -man -Tutf8 -ww -z ${OUT}
                ERROR_VARIABLE groff_err
                OUTPUT_QUIET
                RESULT_VARIABLE groff_rc)
if(NOT groff_rc EQUAL 0)
    message(FATAL_ERROR "groff refused the generated man page (${groff_rc}):\n${groff_err}")
endif()
if(NOT groff_err STREQUAL "")
    message(FATAL_ERROR
        "groff warnings on the generated man page — fix docs/kmixdeck.md or the escaping in "
        "docs/generiere-doku.py (_esc), do NOT edit kmixdeck.1:\n${groff_err}")
endif()

message(STATUS "man page: groff clean")
