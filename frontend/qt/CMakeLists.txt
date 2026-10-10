# ASTRA Desktop — Qt 6 Widgets frontend
# Target: Intel HD 3000 / i5 2nd gen / Windows 10
# QPainter only. No QML, no OpenGL, no QGraphicsView.

cmake_minimum_required(VERSION 3.16)
project(ASTRA_DESKTOP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

# QtSvg is OPTIONAL: it renders the thin-stroke outline icons from
# src/resources/icons/. Without it the sidebar still works (text labels).
find_package(Qt6 QUIET COMPONENTS Svg)

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
    src/pages/ResearchPage.cpp
    src/pages/KnowledgePage.cpp
    src/pages/ApprovalPage.cpp
    src/pages/GovernancePage.cpp
    src/pages/IncidentsPage.cpp
    src/pages/RecoveryPage.cpp
    src/pages/SettingsPage.cpp
    src/widgets/InfoCard.cpp
    src/widgets/SignalCard.cpp
    src/widgets/LevelsCard.cpp
    src/widgets/RegimeChip.cpp
    src/widgets/MtfMeter.cpp
    src/widgets/FreshnessBar.cpp
    src/widgets/CandleChart.cpp
    src/widgets/TimeframeSwitcher.cpp
    src/dialogs/ConfirmExitDialog.cpp
    src/widgets/NavButton.cpp
    src/widgets/SvgIcon.cpp
    src/pages/ComingSoonPage.cpp
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
    src/pages/ResearchPage.h
    src/pages/KnowledgePage.h
    src/pages/ApprovalPage.h
    src/pages/GovernancePage.h
    src/pages/IncidentsPage.h
    src/pages/RecoveryPage.h
    src/pages/SettingsPage.h
    src/widgets/InfoCard.h
    src/widgets/SignalCard.h
    src/widgets/LevelsCard.h
    src/widgets/RegimeChip.h
    src/widgets/MtfMeter.h
    src/widgets/FreshnessBar.h
    src/widgets/CandleChart.h
    src/widgets/TimeframeSwitcher.h
    src/dialogs/ConfirmExitDialog.h
    src/widgets/NavButton.h
    src/widgets/SvgIcon.h
    src/pages/ComingSoonPage.h
)

add_executable(astra_desktop WIN32 ${ASTRA_SOURCES} ${ASTRA_HEADERS})

# Compile the existing resources.qrc (single source of truth for resource paths).
set_source_files_properties(src/resources/resources.qrc PROPERTIES AUTORCC ON)

# Windows resource script: embeds the multi-size astra.ico as the .exe icon
# shown in Explorer / taskbar / Alt-Tab. The RC compiler is Windows-only, so
# this must stay guarded or CMake errors on Linux/macOS.
if(WIN32)
    target_sources(astra_desktop PRIVATE
        src/resources/app.rc)
endif()

target_include_directories(astra_desktop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(astra_desktop PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
)

if(TARGET Qt6::Svg)
    target_link_libraries(astra_desktop PRIVATE Qt6::Svg)
    target_compile_definitions(astra_desktop PRIVATE ASTRA_HAVE_QTSVG)
endif()

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

# ──────────────────────────────────────────────────────────────────────────────
# Offscreen snapshot tool (OFF by default).
# Renders every page to PNG under docs/frontend/screenshots/ so visual
# regressions can be reviewed. Shares all app sources (the same widgets) but
# its own main(). Do NOT enable unless you need screenshots.
# ──────────────────────────────────────────────────────────────────────────────
option(BUILD_ASTRA_SNAPSHOT "Build offscreen snapshot tool" OFF)
if(BUILD_ASTRA_SNAPSHOT)
    # Everything except src/main.cpp (the snapshot tool provides its own main).
    set(ASTRA_SNAPSHOT_SOURCES
        src/MainWindow.cpp
        src/api/ApiClient.cpp
        src/api/ApiTypes.cpp
        src/theme/ThemeManager.cpp
        src/pages/DashboardPage.cpp
        src/pages/ChartPage.cpp
        src/pages/HistoryPage.cpp
        src/pages/HealthPage.cpp
        src/pages/ResearchPage.cpp
        src/pages/KnowledgePage.cpp
        src/pages/ApprovalPage.cpp
        src/pages/GovernancePage.cpp
        src/pages/IncidentsPage.cpp
        src/pages/RecoveryPage.cpp
        src/pages/SettingsPage.cpp
        src/widgets/InfoCard.cpp
        src/widgets/SignalCard.cpp
        src/widgets/LevelsCard.cpp
        src/widgets/RegimeChip.cpp
        src/widgets/MtfMeter.cpp
        src/widgets/FreshnessBar.cpp
        src/widgets/CandleChart.cpp
        src/widgets/TimeframeSwitcher.cpp
        src/dialogs/ConfirmExitDialog.cpp
        src/widgets/NavButton.cpp
        src/widgets/SvgIcon.cpp
        src/pages/ComingSoonPage.cpp
        # Resources (AUTORCC): paths resolve to /astra-*.svg, /icons/*, /theme/*
        src/resources/resources.qrc
    )
    add_executable(astra_snapshot
        tools/snapshot.cpp
        ${ASTRA_SNAPSHOT_SOURCES}
        ${ASTRA_HEADERS}
    )
    set_source_files_properties(src/resources/resources.qrc PROPERTIES AUTORCC ON)
    target_include_directories(astra_snapshot PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(astra_snapshot PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Network
    )
    if(TARGET Qt6::Svg)
        target_link_libraries(astra_snapshot PRIVATE Qt6::Svg)
        target_compile_definitions(astra_snapshot PRIVATE ASTRA_HAVE_QTSVG)
    endif()
endif()

# ──────────────────────────────────────────────────────────────────────────────
# Offscreen widget tests (OFF by default; enabled with BUILD_ASTRA_TESTS=ON).
# Constructs the custom-painted widgets under the offscreen QPA platform and
# grabs them, so paint-path defects (e.g. divide-by-zero in a layout
# expression) fail the build instead of only trapping on a live chart.
# ──────────────────────────────────────────────────────────────────────────────
option(BUILD_ASTRA_TESTS "Build offscreen Qt widget tests" OFF)
if(BUILD_ASTRA_TESTS)
    enable_testing()
    add_executable(astra_chart_tests
        tests/CandleChartTests.cpp
        src/widgets/CandleChart.cpp
    )
    target_include_directories(astra_chart_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(astra_chart_tests PRIVATE
        Qt6::Core
        Qt6::Widgets
    )
    add_test(NAME astra_chart_tests COMMAND astra_chart_tests)
    set_tests_properties(astra_chart_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    # HistoryPage summary formatting (Total R / PF format-string regression).
    # Needs the real HistoryPage translation unit; ApiClient pulls in Qt Network.
    add_executable(astra_history_tests
        tests/HistorySummaryTests.cpp
        src/pages/HistoryPage.cpp
        src/api/ApiClient.cpp
    )
    target_include_directories(astra_history_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(astra_history_tests PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Network
    )
    add_test(NAME astra_history_tests COMMAND astra_history_tests)
    set_tests_properties(astra_history_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    # HealthPage status honesty (unknown token must not render green "ONLINE").
    add_executable(astra_health_tests
        tests/HealthStatusTests.cpp
        src/pages/HealthPage.cpp
        src/api/ApiClient.cpp
    )
    target_include_directories(astra_health_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(astra_health_tests PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Network
    )
    add_test(NAME astra_health_tests COMMAND astra_health_tests)
    set_tests_properties(astra_health_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    # SignalCard score honesty: a missing score must not render a fabricated
    # value (ApiClient used to substitute 0.5).
    add_executable(astra_signal_tests
        tests/SignalScoreHonestyTests.cpp
        src/widgets/SignalCard.cpp
        src/widgets/LevelsCard.cpp
    )
    target_include_directories(astra_signal_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(astra_signal_tests PRIVATE
        Qt6::Core
        Qt6::Widgets
    )
    add_test(NAME astra_signal_tests COMMAND astra_signal_tests)
    set_tests_properties(astra_signal_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()
