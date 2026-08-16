cmake_minimum_required(VERSION 3.16)
project(WindowsAppDataManager LANGUAGES CXX)

include(qt.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Concurrent)
find_package(Qt${QT_VERSION_MAJOR}
    COMPONENTS
        Concurrent
        Qml
        Quick
        QuickControls2
    REQUIRED
)
qt_standard_project_setup()

if(QT_VERSION VERSION_GREATER_EQUAL 6.8)
    qt_policy(SET QTP0004 NEW)
endif()

set(BACKEND_SOURCES
    src/core/classifier/DataClassifier.cpp
    src/core/classifier/DataClassifier.h
    src/core/classifier/RiskAssessment.cpp
    src/core/classifier/RiskAssessment.h
    src/core/resolver/AppResolver.cpp
    src/core/resolver/AppResolver.h
    src/core/rules/IdentifierNormalization.h
    src/core/rules/RuleCatalog.cpp
    src/core/rules/RuleCatalog.h
    src/core/rules/RuleLoader.cpp
    src/core/rules/RuleLoader.h
    src/core/scanner/DirectoryScanner.cpp
    src/core/scanner/DirectoryScanner.h
    src/models/ApplicationInfo.h
    src/models/InstallationEvidence.h
    src/models/RuleDefinition.h
    src/models/ScanResult.h
    src/platform/windows/appx/AppxPackageCatalog.cpp
    src/platform/windows/appx/AppxPackageCatalog.h
    src/platform/windows/filesystem/AppDataPaths.cpp
    src/platform/windows/filesystem/AppDataPaths.h
    src/platform/windows/filesystem/ReparsePoint.cpp
    src/platform/windows/filesystem/ReparsePoint.h
    src/platform/windows/registry/InstalledApplicationRegistry.cpp
    src/platform/windows/registry/InstalledApplicationRegistry.h
    src/services/ScanService.cpp
    src/services/ScanService.h
    src/services/SettingsService.cpp
    src/services/SettingsService.h
)

add_library(wam_backend STATIC ${BACKEND_SOURCES})
target_link_libraries(wam_backend PUBLIC Qt::Core Qt::Concurrent)
if(WIN32)
    target_link_libraries(wam_backend PRIVATE windowsapp advapi32)
endif()
target_include_directories(wam_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(QT_VERSION_MAJOR EQUAL 6)
    qt_add_resources(wam_backend "wam_builtin_rules"
        PREFIX "/windowsappdatamanager/rules"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/rules"
        FILES
            rules/schema.json
            rules/builtin/chrome.json
            rules/builtin/chromium.json
            rules/builtin/discord.json
            rules/builtin/vscode.json
            rules/builtin/jetbrains.json
            rules/builtin/windows-temp.json
            rules/builtin/npm-cache.json
    )
endif()

set(PROJECT_SOURCES
    main.cpp
)

if(QT_VERSION VERSION_LESS 5.15)
    qt5_add_resources(PROJECT_SOURCES qml.qrc)
elseif(QT_VERSION VERSION_LESS 6.2)
    qt_add_resources(PROJECT_SOURCES qml.qrc)
endif()

qt_add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

if(QT_VERSION VERSION_GREATER_EQUAL 6.2)
    set_source_files_properties(
        AppStore.qml
        theme/Theme.qml
        theme/Motion.qml
        PROPERTIES QT_QML_SINGLETON_TYPE TRUE
    )

    qt_add_qml_module(${PROJECT_NAME}
        URI windowsappdatamanager
        VERSION 1.0
        RESOURCE_PREFIX "/qt/qml/"
        QML_FILES
            main.qml
            AppStore.qml
            theme/Theme.qml
            theme/Motion.qml
            components/AppCommandBar.qml
            components/AppSidebar.qml
            components/AppStatusBar.qml
            components/EmptyState.qml
            components/InlineNotice.qml
            components/ScanProgressStrip.qml
            components/OverviewStats.qml
            components/DispositionSummary.qml
            components/OverviewApplicationList.qml
            components/IconButton.qml
            components/NavItem.qml
            components/RiskBadge.qml
            components/StatTile.qml
            components/ThemedIcon.qml
            components/ApplicationRow.qml
            components/FilterComboBox.qml
            components/ApplicationFilterBar.qml
            components/ApplicationListHeader.qml
            components/ApplicationDetailContent.qml
            components/ConfidenceIndicator.qml
            components/DataCategoryRow.qml
            components/DetailMetric.qml
            components/DetailPanel.qml
            components/EvidenceRow.qml
            components/PathField.qml
            components/PreferenceOption.qml
            pages/OverviewPage.qml
            pages/ApplicationsPage.qml
            pages/ApplicationDetailPage.qml
            pages/SettingsPage.qml
        SOURCES
            src/qmlmodels/AppBackend.cpp
            src/qmlmodels/AppBackend.h
            src/qmlmodels/ApplicationListModel.cpp
            src/qmlmodels/ApplicationListModel.h
            src/qmlmodels/ApplicationFilterModel.cpp
            src/qmlmodels/ApplicationFilterModel.h
            src/qmlmodels/ScanViewModel.cpp
            src/qmlmodels/ScanViewModel.h
            src/qmlmodels/SettingsViewModel.cpp
            src/qmlmodels/SettingsViewModel.h
        RESOURCES
            resources/Icons/IcBaselineCleaningServices.svg
            resources/Icons/TablerActivityHeartbeat.svg
            resources/Icons/TablerArrowNarrowDown.svg
            resources/Icons/TablerArrowNarrowUp.svg
            resources/Icons/TablerArrowUpRight.svg
            resources/Icons/TablerCancel.svg
            resources/Icons/TablerChartPieFilled.svg
            resources/Icons/TablerCheck.svg
            resources/Icons/TablerChevronDownFilled.svg
            resources/Icons/TablerChevronLeft.svg
            resources/Icons/TablerChevronRight.svg
            resources/Icons/TablerExclamationMark.svg
            resources/Icons/TablerFileFilled.svg
            resources/Icons/TablerFiles.svg
            resources/Icons/TablerFilesFilled.svg
            resources/Icons/TablerFolder.svg
            resources/Icons/TablerFolderFilled.svg
            resources/Icons/TablerHome.svg
            resources/Icons/TablerHomeFilled.svg
            resources/Icons/TablerMoonFilled.svg
            resources/Icons/TablerPlayerPlayFilled.svg
            resources/Icons/TablerPointFilled.svg
            resources/Icons/TablerQuestionMark.svg
            resources/Icons/TablerRefresh.svg
            resources/Icons/TablerSettings.svg
            resources/Icons/TablerSettingsFilled.svg
            resources/Icons/TablerSunFilled.svg
            resources/Icons/TablerX.svg
            resources/Icons/TablerZoom.svg
            resources/Icons/TablerZoomScan.svg
    )
endif()

set_target_properties(${PROJECT_NAME}
    PROPERTIES
        WIN32_EXECUTABLE TRUE
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        wam_backend
        Qt::Concurrent
        Qt::Qml
        Qt::Quick
        Qt::QuickControls2
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/qmlmodels
)

if(BUILD_TESTING)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
    add_executable(wam_backend_tests
        tests/tst_backend.cpp
        src/qmlmodels/ApplicationListModel.cpp
        src/qmlmodels/ApplicationListModel.h
        src/qmlmodels/ApplicationFilterModel.cpp
        src/qmlmodels/ApplicationFilterModel.h
        src/qmlmodels/ScanViewModel.cpp
        src/qmlmodels/ScanViewModel.h
    )
    target_link_libraries(wam_backend_tests PRIVATE wam_backend Qt::Gui Qt::Qml Qt::Test)
    add_test(NAME wam_backend_tests COMMAND wam_backend_tests)
    set_tests_properties(wam_backend_tests PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>"
    )

    add_executable(wam_installation_evidence_tests
        tests/tst_installation_evidence.cpp
    )
    target_link_libraries(wam_installation_evidence_tests PRIVATE wam_backend Qt::Test)
    add_test(NAME wam_installation_evidence_tests COMMAND wam_installation_evidence_tests)
    set_tests_properties(wam_installation_evidence_tests PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>"
    )
endif()

if(WIN32 AND QT_VERSION_MAJOR EQUAL 6 AND TARGET Qt6::windeployqt)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::windeployqt>;$ENV{PATH}"
            $<TARGET_FILE:Qt6::windeployqt>
            $<IF:$<CONFIG:Debug>,--debug,--release>
            --qmldir "${CMAKE_CURRENT_SOURCE_DIR}"
            --no-translations
            $<TARGET_FILE:${PROJECT_NAME}>
        VERBATIM
    )
endif()

if(Qt6_FOUND)
    qt_import_qml_plugins(${PROJECT_NAME})
endif()
