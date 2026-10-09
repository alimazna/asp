# ASTRA Desktop — Qt 6 Widgets frontend
# Target: Intel HD 3000 / i5 2nd gen / Windows 10
# QPainter only. No QML, no OpenGL, no QGraphicsView.

cmake_minimum_required(VERSION 3.16)
project(ASTRA_DESKTOP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

# Qt standard project setup (auto-includes, auto-moc).
# Called BEFORE add_executable so AUTOMOC applies to our target.
qt_standard_project_setup()

# AUTORCC is set explicitly (qt_standard_project_setup only guarantees AUTOMOC)
# so resources.qrc is compiled into the binary.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(ASTRA_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/api/ApiClient.cpp
    src/api/ApiTypes.cpp
    src/theme/ThemeManager.cpp
    src/pages/DashboardPage.cpp
    src/pages/ChartPage.cpp
    src/pages/HistoryPage.cpp
    src/pages/HealthPage.cpp
    src/pages/SettingsPage.cpp
    src/widgets/SignalCard.cpp
    src/widgets/LevelsCard.cpp
    src/widgets/RegimeChip.cpp
    src/widgets/MtfMeter.cpp
    src/widgets/FreshnessBar.cpp
    src/widgets/CandleChart.cpp
    src/widgets/TimeframeSwitcher.cpp
    src/dialogs/ConfirmExitDialog.cpp
    # Resources (AUTORCC): paths resolve to /astra-*.svg, /icons/*, /theme/*
    src/resources/resources.qrc
)

set(ASTRA_HEADERS
    src/MainWindow.h
    src/api/ApiClient.h
    src/api/ApiTypes.h
    src/theme/ThemeManager.h
    src/pages/DashboardPage.h
    src/pages/ChartPage.h
    src/pages/HistoryPage.h
    src/pages/HealthPage.h
    src/pages/SettingsPage.h
    src/widgets/SignalCard.h
    src/widgets/LevelsCard.h
    src/widgets/RegimeChip.h
    src/widgets/MtfMeter.h
    src/widgets/FreshnessBar.h
    src/widgets/CandleChart.h
    src/widgets/TimeframeSwitcher.h
    src/dialogs/ConfirmExitDialog.h
)

add_executable(astra_desktop WIN32 ${ASTRA_SOURCES} ${ASTRA_HEADERS})

# Compile the existing resources.qrc (single source of truth for resource paths).
set_source_files_properties(src/resources/resources.qrc PROPERTIES AUTORCC ON)

target_include_directories(astra_desktop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(astra_desktop PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
)

if(WIN32)
    set_target_properties(astra_desktop PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        WIN32_EXECUTABLE ON
    )
endif()

# Install rule (used by windeployqt)
install(TARGETS astra_desktop
    RUNTIME DESTINATION .
)
