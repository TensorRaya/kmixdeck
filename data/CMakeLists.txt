configure_file(kmixdeckd.service.in ${CMAKE_CURRENT_BINARY_DIR}/kmixdeckd.service @ONLY)
configure_file(org.kmixdeck1.service.in ${CMAKE_CURRENT_BINARY_DIR}/org.kmixdeck1.service @ONLY)
configure_file(kmixdeck-web.service.in ${CMAKE_CURRENT_BINARY_DIR}/kmixdeck-web.service @ONLY)   # PP-1: optional, disabled by default

# systemd --user searches $XDG_DATA_HOME/systemd/user for per-user installs, but lib/systemd/user only under
# /usr and /usr/local (systemd.unit(5) "Unit File Load Path"). A prefix inside $HOME therefore needs share/.
if(CMAKE_INSTALL_PREFIX MATCHES "^$ENV{HOME}/")
    set(_kmixdeck_unitdir share/systemd/user)
else()
    set(_kmixdeck_unitdir ${KDE_INSTALL_SYSTEMDUSERUNITDIR})
endif()
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kmixdeckd.service ${CMAKE_CURRENT_BINARY_DIR}/kmixdeck-web.service DESTINATION ${_kmixdeck_unitdir})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kmixdeck1.service DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR})
install(FILES ${CMAKE_SOURCE_DIR}/interfaces/org.kmixdeck1.Mixer.xml ${CMAKE_SOURCE_DIR}/interfaces/org.kmixdeck1.Channel.xml
              ${CMAKE_SOURCE_DIR}/interfaces/org.kmixdeck1.Mix.xml ${CMAKE_SOURCE_DIR}/interfaces/org.kmixdeck1.Cell.xml
              ${CMAKE_SOURCE_DIR}/interfaces/org.kmixdeck1.App.xml
        DESTINATION ${KDE_INSTALL_DBUSINTERFACEDIR})
# ADR 0014 HY-5: the Waybar module block. The CLI is always built, so this does not depend on the KDE frontend.
install(FILES waybar/kmixdeck.jsonc DESTINATION ${KDE_INSTALL_DATADIR}/kmixdeck/waybar)
if(BUILD_KDE_FRONTEND)
    install(FILES org.kmixdeck.kmixdeck.desktop DESTINATION ${KDE_INSTALL_APPDIR})
    install(FILES org.kmixdeck.kmixdeck.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR})
    install(FILES kmixdeck.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
    # ADR 0014 HY-3: require()-able from the user's hyprland.lua; never written into the user's config by us.
    install(FILES hyprland/kmixdeck.lua DESTINATION ${KDE_INSTALL_DATADIR}/kmixdeck/hyprland)
endif()
