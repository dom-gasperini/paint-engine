cmake_minimum_required(VERSION 3.21)

project(PaintEngine
    VERSION 1.0.1
    DESCRIPTION "Paint product similarity search"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PAINTENGINE_BUILD_TESTS "Build the console tests for the core library" ON)

# ---------------------------------------------------------------- core (no Qt)
add_library(paint_core STATIC
    core/paint_match.hpp
    core/csv_reader.hpp
    core/csv_reader.cpp
    core/paint_schema.hpp
    core/paint_schema.cpp
)
target_include_directories(paint_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
if(MSVC)
    # Sources contain UTF-8 text (degree sign, dashes)
    target_compile_options(paint_core PUBLIC /utf-8)
    target_compile_options(paint_core PRIVATE /W4)
else()
    target_compile_options(paint_core PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------- application
find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

set(CMAKE_AUTOMOC ON)

set(APP_SOURCES
    app/main.cpp
    app/AppInfo.h
    app/AppSettings.h
    app/AppSettings.cpp
    app/CriteriaPanel.h
    app/CriteriaPanel.cpp
    app/InputWidgets.h
    app/InputWidgets.cpp
    app/MainWindow.h
    app/MainWindow.cpp
    app/ResultsModel.h
    app/ResultsModel.cpp
    app/SettingsDialog.h
    app/SettingsDialog.cpp
)

if(WIN32)
    configure_file(resources/version.rc.in ${CMAKE_CURRENT_BINARY_DIR}/version.rc @ONLY)
    list(APPEND APP_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
endif()

add_executable(PaintEngine WIN32 ${APP_SOURCES})
target_link_libraries(PaintEngine PRIVATE paint_core Qt6::Widgets)

# Window / taskbar icon used at runtime (":/pwp_logo.png"). The .exe icon itself comes
# from resources/version.rc.in.
qt_add_resources(PaintEngine "app_icons"
    PREFIX "/"
    BASE resources
    FILES resources/pwp_logo.png)
target_compile_definitions(PaintEngine PRIVATE APP_VERSION="${PROJECT_VERSION}")

# Copy the sample database next to the executable so it runs straight from the build folder.
add_custom_command(TARGET PaintEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:PaintEngine>/data"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/data/paints.csv"
            "$<TARGET_FILE_DIR:PaintEngine>/data/paints.csv"
    COMMENT "Copying sample paint database")

# `cmake --install <build> --prefix installer/packages/com.paintengine.app/data`
# stages the files for the Qt Installer Framework package.
install(TARGETS PaintEngine RUNTIME DESTINATION .)
install(FILES data/paints.csv DESTINATION data)

# ---------------------------------------------------------------- tests
if(PAINTENGINE_BUILD_TESTS)
    enable_testing()
    add_executable(core_tests tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE paint_core)
    add_test(NAME core_tests
             COMMAND core_tests "${CMAKE_CURRENT_SOURCE_DIR}/data/paints.csv")
endif()
