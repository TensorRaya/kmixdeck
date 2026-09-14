# Core (PipeWire layer + mixer model) as a static lib so tests link it without the QML app.
add_library(kmixdeck_core STATIC
    mixer.cpp mixer.h
    pipewire/graph.cpp pipewire/graph.h
)
target_include_directories(kmixdeck_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kmixdeck_core PUBLIC Qt6::Core PkgConfig::PipeWire)
target_compile_options(kmixdeck_core PRIVATE -Wall -Wextra)

add_executable(kmixdeck main.cpp)

ecm_add_qml_module(kmixdeck
    URI org.kmixdeck
    VERSION 1.0
    GENERATE_PLUGIN_SOURCE
    QML_FILES
        qml/Main.qml
        qml/MixerPage.qml
        qml/CellFader.qml
        qml/AddDialog.qml
)
target_sources(kmixdeck PRIVATE qmltypes.h)
target_include_directories(kmixdeck PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/..)

target_link_libraries(kmixdeck PRIVATE
    kmixdeck_core
    Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Svg Qt6::DBus
    KF6::CoreAddons KF6::ConfigCore KF6::I18n KF6::IconThemes
    KF6::GlobalAccel KF6::StatusNotifierItem KF6::Notifications KF6::DBusAddons KF6::Crash
)
target_compile_options(kmixdeck PRIVATE -Wall -Wextra)

install(TARGETS kmixdeck ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
