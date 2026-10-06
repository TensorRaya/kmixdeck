# ADR 0015: PipeWire's filter-chain module from 1.6.3, built only against 1.6.0-1.6.2 (#5202). See README.md here.
#
# Compile flags: the ones PipeWire's own meson.build gives this file (common_flags + cc_flags, c_std=gnu11), so the
# module is built the way the distribution builds the original. SPA_AUDIO_MAX_CHANNELS=128u is part of that: PipeWire
# compiles every module with it, while the installed header defaults to 64u.
#
# The parent directory is a KDE/Qt project: KDECompilerSettings adds Qt definitions (QT_NO_CAST_*, …) and turns on
# AUTOMOC, which compiled an empty mocs_compilation.cpp into this C module and made it link with c++ (measured
# 2026-10-06, `ninja -t commands`). Neither belongs to a PipeWire module, so this directory starts clean.
set_directory_properties(PROPERTIES COMPILE_DEFINITIONS "")
add_library(kmixdeck-filter-chain MODULE module-filter-chain.c)
set_target_properties(kmixdeck-filter-chain PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    LINKER_LANGUAGE C
    OUTPUT_NAME libpipewire-module-kmixdeck-filter-chain
    PREFIX ""
    C_STANDARD 11
    C_EXTENSIONS ON
    C_VISIBILITY_PRESET hidden
    # A directory with nothing but this module: tests put it in front of PIPEWIRE_MODULE_DIR.
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib/pipewire-0.3)
# config.h: the source includes it for PACKAGE_VERSION only (shown as module.version in pw-cli). The source is 1.6.3.
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/config.h "#define PACKAGE_VERSION \"1.6.3-kmixdeck\"\n")
target_include_directories(kmixdeck-filter-chain PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(kmixdeck-filter-chain PRIVATE _GNU_SOURCE FASTPATH SPA_AUDIO_MAX_CHANNELS=128u)
target_compile_options(kmixdeck-filter-chain PRIVATE
    -fno-strict-aliasing -fno-strict-overflow
    -Wno-unused-parameter -Wno-missing-field-initializers -Wno-pedantic)
target_link_libraries(kmixdeck-filter-chain PRIVATE PkgConfig::PipeWire m)

# PipeWire loads modules only from PIPEWIRE_MODULE_DIR or its compiled-in module directory, never from an absolute
# path (pw_context_load_module → find_module; absolute names are refused since 6bc07dfe0e). So the module goes into
# that directory, whatever CMAKE_INSTALL_PREFIX is; DESTDIR still applies for packaging.
if(CMAKE_INSTALL_PREFIX MATCHES "^$ENV{HOME}/")
    # A prefix inside $HOME cannot write /usr/lib/…/pipewire-0.3. Keep it out of the default install so a user
    # install works (same reason as the bash-completion path, see the top-level CMakeLists.txt), and say so.
    install(TARGETS kmixdeck-filter-chain LIBRARY DESTINATION ${PIPEWIRE_MODULEDIR}
            COMPONENT pipewire-module EXCLUDE_FROM_ALL)
    message(WARNING "PipeWire ${PipeWire_VERSION} has the filter-chain crash #5202. The fixed module is not part of "
                    "this install (prefix inside $HOME). Install it with: "
                    "sudo cmake --install ${CMAKE_BINARY_DIR} --component pipewire-module")
else()
    install(TARGETS kmixdeck-filter-chain LIBRARY DESTINATION ${PIPEWIRE_MODULEDIR} COMPONENT pipewire-module)
endif()
