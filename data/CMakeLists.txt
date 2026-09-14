configure_file(kmixdeckd.service.in ${CMAKE_CURRENT_BINARY_DIR}/kmixdeckd.service @ONLY)
configure_file(org.kmixdeck1.service.in ${CMAKE_CURRENT_BINARY_DIR}/org.kmixdeck1.service @ONLY)

# systemd --user searches $XDG_DATA_HOME/systemd/user for per-user installs, but lib/systemd/user only under
# /usr and /usr/local (systemd.unit(5) "Unit File Load Path"). A prefix inside $HOME therefore needs share/.
if(CMAKE_INSTALL_PREFIX MATCHES "^$ENV{HOME}/")
    set(_kmixdeck_unitdir share/systemd/user)
else()
    set(_kmixdeck_unitdir ${KDE_INSTALL_SYSTEMDUSERUNITDIR})
endif()
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kmixdeckd.service DESTINATION ${_kmixdeck_unitdir})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kmixdeck1.service DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR})
install(FILES ${CMAKE_SOURCE_DIR}/interfaces/org.kmixdeck1.Mixer.xml ${CMAKE_SOURCE_DIR}/interfaces/org.kmixdeck1.Channel.xml
              ${CMAKE_SOURCE_DIR}/interfaces/org.kmixdeck1.Mix.xml ${CMAKE_SOURCE_DIR}/interfaces/org.kmixdeck1.Cell.xml
              ${CMAKE_SOURCE_DIR}/interfaces/org.kmixdeck1.App.xml
        DESTINATION ${KDE_INSTALL_DBUSINTERFACEDIR})
install(FILES org.kmixdeck.kmixdeck.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES org.kmixdeck.kmixdeck.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR})
install(FILES kmixdeck.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
