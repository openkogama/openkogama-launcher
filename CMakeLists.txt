cmake_minimum_required(VERSION 3.19)
project(openkogama-launcher LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network)

qt_standard_project_setup()

qt_add_executable(openkogama-launcher
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
)

target_link_libraries(openkogama-launcher
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Network
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
