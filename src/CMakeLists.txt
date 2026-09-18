# --- core: PipeWire layer + mixer model (QML-free, used by the daemon and by unit tests)
add_library(kmixdeck_core STATIC
    logging.cpp logging.h
    mixer.cpp mixer.h
    layout.cpp layout.h
    fx.cpp fx.h
    pipewire/graph.cpp pipewire/graph.h
    pipewire/meters.cpp pipewire/meters.h
)
target_include_directories(kmixdeck_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kmixdeck_core PUBLIC Qt6::Core PkgConfig::PipeWire)
target_compile_options(kmixdeck_core PRIVATE -Wall -Wextra)

# --- kmixdeckd: the service (ADR 0005). Only binary that links libpipewire.
add_executable(kmixdeckd
    daemon/main.cpp
    daemon/service.cpp daemon/service.h
    daemon/objectmanager.h
)
target_include_directories(kmixdeckd PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/.. daemon)
target_link_libraries(kmixdeckd PRIVATE kmixdeck_core Qt6::DBus KF6::CoreAddons)   # KSignalHandler
target_compile_options(kmixdeckd PRIVATE -Wall -Wextra)
if(CMAKE_LIBRARY_ARCHITECTURE)
    target_compile_definitions(kmixdeck_core PRIVATE KMIXDECK_MULTIARCH="${CMAKE_LIBRARY_ARCHITECTURE}")
endif()
install(TARGETS kmixdeckd DESTINATION ${KDE_INSTALL_LIBEXECDIR})

# --- kmixdeck: the CLI. Pure D-Bus client, no PipeWire.
add_executable(kmixdeck-cli cli/main.cpp)
set_target_properties(kmixdeck-cli PROPERTIES OUTPUT_NAME kmixdeck)
target_include_directories(kmixdeck-cli PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/..)
target_link_libraries(kmixdeck-cli PRIVATE Qt6::Core Qt6::DBus)
target_compile_options(kmixdeck-cli PRIVATE -Wall -Wextra)
# CLI is literal-heavy; the KDE ASCII-cast ban buys nothing here.
remove_definitions(-DQT_NO_CAST_FROM_ASCII -DQT_NO_CAST_TO_ASCII -DQT_NO_CAST_FROM_BYTEARRAY -DQT_USE_QSTRINGBUILDER)
set_property(TARGET kmixdeck-cli PROPERTY COMPILE_DEFINITIONS "")
install(TARGETS kmixdeck-cli ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

# --- kmixdeck-kde: the Kirigami frontend. Pure D-Bus client, no PipeWire (AR-2/AR-6 enforced by the linker).
add_executable(kmixdeck-kde main.cpp frontend/mixerclient.cpp frontend/mixerclient.h frontend/kdeintegration.cpp frontend/kdeintegration.h frontend/iconpicker.cpp frontend/iconpicker.h qmltypes.h)
ecm_add_qml_module(kmixdeck-kde
    URI org.kmixdeck
    VERSION 1.0
    GENERATE_PLUGIN_SOURCE
    QML_FILES
        qml/Main.qml
        qml/MixerPage.qml
        qml/CellFader.qml
        qml/AddDialog.qml
        qml/AppsPage.qml
        qml/RoutingPage.qml
        qml/PatchBayPage.qml
        qml/PortPicker.qml
        qml/MixHeader.qml
        qml/ChannelHeader.qml
        qml/RenameDialog.qml
        qml/IconDialog.qml
        qml/FxPanel.qml
        qml/Fader.qml
        qml/LevelMeter.qml
        qml/TrayOverview.qml
        qml/ColorMenu.qml
        qml/FirstRunDialog.qml
)
target_include_directories(kmixdeck-kde PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/.. frontend)
target_link_libraries(kmixdeck-kde PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Svg Qt6::DBus
    KF6::CoreAddons KF6::ConfigCore KF6::I18n KF6::IconThemes KF6::IconWidgets
    KF6::GlobalAccel KF6::StatusNotifierItem KF6::Notifications KF6::DBusAddons KF6::Crash
)
target_compile_options(kmixdeck-kde PRIVATE -Wall -Wextra)
install(TARGETS kmixdeck-kde ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
