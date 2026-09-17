cmake_minimum_required(VERSION 3.20)
project(kmixdeck VERSION 0.1.0 LANGUAGES CXX)

set(QT_MIN_VERSION 6.6.0)
set(KF_MIN_VERSION 6.0.0)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMSetupVersion)
include(ECMQmlModule)
include(FeatureSummary)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

ecm_setup_version(${PROJECT_VERSION}
    VARIABLE_PREFIX KMIXDECK
    VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/kmixdeck_version.h")

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2 Svg DBus)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    CoreAddons Config I18n Kirigami IconThemes
    GlobalAccel StatusNotifierItem Notifications DBusAddons Crash)
find_package(KF6KirigamiAddons REQUIRED)
find_package(KF6QQC2DesktopStyle REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PipeWire REQUIRED IMPORTED_TARGET libpipewire-0.3>=1.0)

# UX-5: catalogs in po/<lang>/kmixdeck.po → <KDE_INSTALL_LOCALEDIR>/kmixdeck/<lang>/LC_MESSAGES/kmixdeck.mo
ki18n_install(po)

add_subdirectory(src)
add_subdirectory(data)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
