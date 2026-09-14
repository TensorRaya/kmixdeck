configure_file(kmixdeckd.service.in ${CMAKE_CURRENT_BINARY_DIR}/kmixdeckd.service @ONLY)
configure_file(org.kmixdeck1.service.in ${CMAKE_CURRENT_BINARY_DIR}/org.kmixdeck1.service @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kmixdeckd.service DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.kmixdeck1.service DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR})
install(FILES ${CMAKE_SOURCE_DIR}/interfaces/org.kmixdeck1.Mixer.xml ${CMAKE_SOURCE_DIR}/interfaces/org.kmixdeck1.Channel.xml
              ${CMAKE_SOURCE_DIR}/interfaces/org.kmixdeck1.Mix.xml ${CMAKE_SOURCE_DIR}/interfaces/org.kmixdeck1.Cell.xml
              ${CMAKE_SOURCE_DIR}/interfaces/org.kmixdeck1.App.xml
        DESTINATION ${KDE_INSTALL_DBUSINTERFACEDIR})
install(FILES org.kmixdeck.kmixdeck.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES org.kmixdeck.kmixdeck.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR})
install(FILES kmixdeck.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
