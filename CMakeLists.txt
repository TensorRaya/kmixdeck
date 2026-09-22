cmake_minimum_required(VERSION 3.20)
project(kmixdeck VERSION 0.3.0 LANGUAGES CXX)

set(QT_MIN_VERSION 6.6.0)
set(KF_MIN_VERSION 6.0.0)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)   # review 2026-09-18 CC-2: the build is warning-free; keep it that way
include(ECMSetupVersion)
include(ECMQmlModule)
include(FeatureSummary)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Warnings on by default (review v0.3, 2026-09-20): without these, a build reporting "0 warnings" says almost
# nothing — it only means the compiler was asked nothing. Measured against the whole tree at that point: 90/90
# targets, zero warnings, so this costs nothing today and catches the next mistake. Deliberately NOT -Werror:
# a new compiler version must not break a user's build over a warning we have never seen.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

ecm_setup_version(${PROJECT_VERSION}
    VARIABLE_PREFIX KMIXDECK
    VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/kmixdeck_version.h")

# Headless boxes (a streaming host with the Ui24R on USB, no desktop) build the service + CLI + web bridge only.
option(BUILD_KDE_FRONTEND "Build the Kirigami window/tray (needs Kirigami, GlobalAccel, StatusNotifierItem)" ON)
add_feature_info(KDE_FRONTEND BUILD_KDE_FRONTEND "kmixdeck-kde window and tray")

if(BUILD_KDE_FRONTEND)
    find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2 Svg DBus)
    find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
        CoreAddons Config I18n Kirigami IconThemes
        GlobalAccel StatusNotifierItem Notifications DBusAddons Crash)
    find_package(KF6KirigamiAddons REQUIRED)
    find_package(KF6QQC2DesktopStyle REQUIRED)
else()
    find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core DBus)
    find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS CoreAddons I18n)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(PipeWire REQUIRED IMPORTED_TARGET libpipewire-0.3>=1.0)
# UX-18: EBU R128 loudness (libebur128). Required, not optional: a loudness meter that silently is not there
# is worse than a build error — the streamer would trust an empty reading.
pkg_check_modules(EBUR128 REQUIRED IMPORTED_TARGET libebur128>=1.2)
# CT-8 (soundboard): sample decoding via ffmpeg. Required for the same reason as libebur128 — a soundboard
# that silently cannot read mp3 is worse than a build error. Chosen over libsndfile (not installed, and no
# mp3/ogg without extras), Qt6Multimedia (would drag QtGui into the headless daemon, AR-2) and the
# vorbisfile+mpg123+opusfile trio (three dependencies instead of one). libswresample also brings the rate
# conversion we need anyway: a sample's rate is whatever the file has, the graph's is whatever the session
# negotiated. See specs/ct8-soundboard.md.
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat>=60 libavcodec>=60 libswresample>=4 libavutil>=58)

# UX-5: catalogs in po/<lang>/kmixdeck.po → <KDE_INSTALL_LOCALEDIR>/kmixdeck/<lang>/LC_MESSAGES/kmixdeck.mo
ki18n_install(po)

# CT-3: OpenAction / Stream Deck plugin. Installed where OpenDeck looks for system-wide plugins is not standardised, so
# it goes to the data dir; `kmixdeck streamdeck install` (or a symlink) puts it into the user's OpenDeck plugins folder.
install(DIRECTORY streamdeck/me.kmixdeck.sdPlugin DESTINATION ${KDE_INSTALL_DATADIR}/kmixdeck/streamdeck
        USE_SOURCE_PERMISSIONS PATTERN "plugin.log" EXCLUDE PATTERN "__pycache__" EXCLUDE)

# AR-8: browser frontend. The bridge is a python script (python3-gi + python3-websockets at runtime); the UI is static files.
install(PROGRAMS web/kmixdeck-web DESTINATION ${KDE_INSTALL_BINDIR})
install(DIRECTORY web/static/ DESTINATION ${KDE_INSTALL_DATADIR}/kmixdeck/web)

# CL-7: shell completion for bash and zsh. Both scripts are deliberately thin — they carry no command list of their
# own but ask `kmixdeck complete`, which answers from the same table the CLI dispatches on. A completion with its own
# list drifts silently: `tree` and `patch` arrived on 2026-09-21 and no test would have caught a stale list, because a
# missing completion is invisible.
#
# Paths: bash-completion's own pkg-config variable when the package is installed (that is where the distro wants it),
# otherwise the documented fallback share/bash-completion/completions. zsh has no pkg-config file, so site-functions
# under the install prefix is the only correct answer; a file named `_kmixdeck` there is picked up by compinit.
find_package(bash-completion QUIET)
if(BASH_COMPLETION_COMPLETIONSDIR)
    set(_kmixdeck_bashdir "${BASH_COMPLETION_COMPLETIONSDIR}")
else()
    set(_kmixdeck_bashdir "${KDE_INSTALL_DATADIR}/bash-completion/completions")
endif()
install(FILES shell/kmixdeck.bash DESTINATION ${_kmixdeck_bashdir} RENAME kmixdeck)
install(FILES shell/_kmixdeck DESTINATION ${KDE_INSTALL_DATADIR}/zsh/site-functions)

# CL-1: kmixdeck.1 is GENERATED from docs/kmixdeck.md, never edited by hand and not checked in — one source for the
# man page, `--help` and docs/cli.md, so the three cannot drift apart. docs/generiere-doku.py also enforces the
# man-pages(7) section order and rejects invented section names, and `pruefen` runs as a test (see tests/).
# 🔴 add_custom_command has NO shell and NO OUTPUT_FILE: `> file` and `OUTPUT_FILE file` both end up as literal
# arguments to the script (verified 2026-09-21 — the target built "successfully" and wrote nothing). The redirect
# has to happen inside a -P script, which is what erzeuge-manpage.cmake is for.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(KMIXDECK_MAN ${CMAKE_CURRENT_BINARY_DIR}/kmixdeck.1)
add_custom_command(OUTPUT ${KMIXDECK_MAN}
        COMMAND ${CMAKE_COMMAND}
                -DPYTHON3=${Python3_EXECUTABLE}
                -DGENERATOR=${CMAKE_CURRENT_SOURCE_DIR}/docs/generiere-doku.py
                -DVERSION=${PROJECT_VERSION}
                -DOUT=${KMIXDECK_MAN}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/erzeuge-manpage.cmake
        DEPENDS docs/kmixdeck.md docs/generiere-doku.py cmake/erzeuge-manpage.cmake
        COMMENT "Generating kmixdeck.1 from docs/kmixdeck.md"
        VERBATIM)
add_custom_target(manpage ALL DEPENDS ${KMIXDECK_MAN})
install(FILES ${KMIXDECK_MAN} DESTINATION ${KDE_INSTALL_MANDIR}/man1)

add_subdirectory(src)
add_subdirectory(data)

if(BUILD_TESTING)
    # 🔴 appstreamtest wird von KDECMakeSettings.cmake angelegt, nicht von uns — und kam
    # deshalb ohne Label. Ein Test ohne Label fiel bei `ctest -L fast` UND bei
    # `ctest -L integration` durchs Raster: gemessen 2026-09-21 liefen 9 + 10 von 22
    # Tests, drei liefen in KEINEM Gate. Ein Label-Filter ist fail-open, und genau das
    # macht ihn als Gate gefaehrlich. Hier nachgetragen, weil der Test 0,08 s braucht.
    set_tests_properties(appstreamtest PROPERTIES LABELS "fast")
    add_subdirectory(tests)
endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
