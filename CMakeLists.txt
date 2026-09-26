cmake_minimum_required(VERSION 3.19)
project(openkogama-launcher LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core CorePrivate Widgets Network Concurrent)

qt_standard_project_setup()

qt_add_executable(openkogama-launcher
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    app.rc
    installdialog.h installdialog.cpp installdialog.ui
    installprogressdialog.h installprogressdialog.cpp installprogressdialog.ui
    launchdialog.h launchdialog.cpp launchdialog.ui
    worldsdialog.h worldsdialog.cpp worldsdialog.ui
    webplayerruntime.h webplayerruntime.cpp
    settings.h settings.cpp
    settingsdialog.h settingsdialog.cpp settingsdialog.ui
    consolewindow.h consolewindow.cpp
    centereddelegate.h
)

qt_add_resources(openkogama-launcher "resources"
    PREFIX "/"
    FILES versions.json
    FILES discord.png
    FILES install.png
    FILES unknown.png
    FILES launch.png
)

target_link_libraries(openkogama-launcher
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Network
        Qt::CorePrivate
        Qt::Concurrent
)

install(TARGETS openkogama-launcher
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET openkogama-launcher
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
