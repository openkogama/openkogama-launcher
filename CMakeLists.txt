cmake_minimum_required(VERSION 3.19)
project(openkogama-launcher LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core CorePrivate Widgets Network Concurrent LinguistTools)

qt_standard_project_setup()

qt_add_executable(openkogama-launcher
    WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/mainwindow.ui
    resources/app.rc
    src/installdialog.h src/installdialog.cpp src/installdialog.ui
    src/installprogressdialog.h src/installprogressdialog.cpp src/installprogressdialog.ui
    src/launchdialog.h src/launchdialog.cpp src/launchdialog.ui
    src/assetinstaller.h src/assetinstaller.cpp
    src/worldsdialog.h src/worldsdialog.cpp src/worldsdialog.ui
    src/gamelauncher.h src/gamelauncher.cpp
    src/singleinstance.h src/singleinstance.cpp
    src/clientpatches.h src/clientpatches.cpp
    src/serverfeatures.h src/serverfeatures.cpp
    src/playerprofile.h src/playerprofile.cpp
    src/discordpresence.h src/discordpresence.cpp
    src/seasonalknock.h src/seasonalknock.cpp
    src/links.h src/links.cpp
    src/serverspage.h src/serverspage.cpp
    src/tunnel.h src/tunnel.cpp
    src/sharebar.h src/sharebar.cpp
    src/serverconfigdialog.h src/serverconfigdialog.cpp
    src/admindialog.h src/admindialog.cpp
    src/tilegrid.h src/tilegrid.cpp
    src/modelpreview.h src/modelpreview.cpp
    src/webplayerruntime.h src/webplayerruntime.cpp
    src/paths.h src/paths.cpp
    src/updater.h src/updater.cpp
    src/theme.h src/theme.cpp
    src/settings.h src/settings.cpp
    src/languages.h src/languages.cpp
    src/settingsdialog.h src/settingsdialog.cpp src/settingsdialog.ui
    src/account.h src/account.cpp
    src/consolewindow.h src/consolewindow.cpp
    src/centereddelegate.h
    src/creditsdialog.h src/creditsdialog.cpp
)

qt_add_resources(openkogama-launcher "resources"
    PREFIX "/"
    BASE resources
    FILES
        resources/versions.json
        resources/discord.png
        resources/install.png
        resources/unknown.png
        resources/launch.png
        resources/lemonplus.png
        resources/logo.png
        resources/openkogama-font.ttf
        resources/launcher.qss
        resources/halloween-knock.wav
        resources/arrow-down.svg
        resources/check.svg
        resources/plus.svg
        resources/plus-light.svg
        resources/arrow-down-light.svg
)

include(FetchContent)
FetchContent_Declare(velopack
    URL https://github.com/velopack/velopack/releases/download/1.2.161/velopack_libc_1.2.161.zip
    URL_HASH SHA256=31de8f2cf05a47bd5328a9f98b0c41aec1582156f6075f1058b26941f7f17c20
    DOWNLOAD_EXTRACT_TIMESTAMP ON
)
FetchContent_MakeAvailable(velopack)
configure_file(${velopack_SOURCE_DIR}/lib/velopack_libc_win_x64_msvc.dll ${CMAKE_CURRENT_BINARY_DIR}/velopack_libc.dll COPYONLY)

add_library(velopack SHARED IMPORTED)
set_target_properties(velopack PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/velopack_libc.dll
    IMPORTED_IMPLIB ${velopack_SOURCE_DIR}/lib/velopack_libc_win_x64_msvc.dll.lib
    INTERFACE_INCLUDE_DIRECTORIES ${velopack_SOURCE_DIR}/include
)

target_link_libraries(openkogama-launcher
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Network
        Qt::CorePrivate
        Qt::Concurrent
        velopack
        crypt32
        winmm
)

add_custom_command(TARGET openkogama-launcher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:velopack> $<TARGET_FILE_DIR:openkogama-launcher>
)

file(GLOB LAUNCHER_TRANSLATIONS CONFIGURE_DEPENDS i18n/launcher_*.ts)
list(REMOVE_ITEM LAUNCHER_TRANSLATIONS ${CMAKE_CURRENT_SOURCE_DIR}/i18n/launcher_en.ts)
qt_add_translations(openkogama-launcher
    TS_FILES i18n/launcher.ts ${LAUNCHER_TRANSLATIONS}
    PLURALS_TS_FILE i18n/launcher_en.ts
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -no-obsolete -locations none
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
install(FILES $<TARGET_FILE:velopack> DESTINATION ${CMAKE_INSTALL_BINDIR})

option(BUILD_TESTING "Build launcher tests" OFF)
include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(knock-tests tests/seasonalknock_test.cpp)
    target_include_directories(knock-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(knock-tests PRIVATE Qt::Core)
    add_test(NAME launcher-knock COMMAND knock-tests)
    qt_add_executable(launcher-tests tests/serverspage_test.cpp src/serverspage.cpp src/serverspage.h src/tilegrid.cpp src/tilegrid.h src/settings.cpp src/settings.h src/tunnel.cpp src/tunnel.h src/sharebar.cpp src/sharebar.h src/serverconfigdialog.cpp src/serverconfigdialog.h
        src/gamelauncher.cpp src/gamelauncher.h src/singleinstance.cpp src/singleinstance.h src/clientpatches.cpp src/clientpatches.h src/serverfeatures.cpp src/serverfeatures.h src/account.cpp src/account.h src/playerprofile.cpp src/playerprofile.h src/discordpresence.cpp src/discordpresence.h src/consolewindow.cpp src/consolewindow.h src/webplayerruntime.cpp src/webplayerruntime.h src/paths.cpp src/paths.h src/theme.cpp src/theme.h
        src/launchdialog.cpp src/launchdialog.h src/launchdialog.ui src/assetinstaller.cpp src/assetinstaller.h
        src/seasonalknock.cpp src/seasonalknock.h)
    target_include_directories(launcher-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(launcher-tests PRIVATE LAUNCHER_TRANSLATIONS="${CMAKE_CURRENT_BINARY_DIR}")
    add_dependencies(launcher-tests openkogama-launcher_lrelease)
    target_link_libraries(launcher-tests PRIVATE Qt::Widgets Qt::Network Qt::Test Qt::CorePrivate crypt32 winmm)
    qt_add_resources(launcher-tests "test_resources" PREFIX "/" BASE resources FILES resources/logo.png)
    add_test(NAME launcher-servers COMMAND launcher-tests -o ${CMAKE_CURRENT_BINARY_DIR}/launcher-tests.txt,txt)
    set_tests_properties(launcher-servers PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
