cmake_minimum_required(VERSION 3.20)
project(kmixdeck VERSION 0.1.1 LANGUAGES CXX)

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

# UX-5: catalogs in po/<lang>/kmixdeck.po → <KDE_INSTALL_LOCALEDIR>/kmixdeck/<lang>/LC_MESSAGES/kmixdeck.mo
ki18n_install(po)

# CT-3: OpenAction / Stream Deck plugin. Installed where OpenDeck looks for system-wide plugins is not standardised, so
# it goes to the data dir; `kmixdeck streamdeck install` (or a symlink) puts it into the user's OpenDeck plugins folder.
install(DIRECTORY streamdeck/me.kmixdeck.sdPlugin DESTINATION ${KDE_INSTALL_DATADIR}/kmixdeck/streamdeck
        USE_SOURCE_PERMISSIONS PATTERN "plugin.log" EXCLUDE PATTERN "__pycache__" EXCLUDE)

# AR-8: browser frontend. The bridge is a python script (python3-gi + python3-websockets at runtime); the UI is static files.
install(PROGRAMS web/kmixdeck-web DESTINATION ${KDE_INSTALL_BINDIR})
install(DIRECTORY web/static/ DESTINATION ${KDE_INSTALL_DATADIR}/kmixdeck/web)

add_subdirectory(src)
add_subdirectory(data)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
