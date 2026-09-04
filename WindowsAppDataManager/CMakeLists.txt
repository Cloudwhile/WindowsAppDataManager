cmake_minimum_required(VERSION 3.16)
project(WindowsAppDataManager LANGUAGES CXX)

include(qt.cmake)

set(WAM_VERSION "0.1.0" CACHE STRING "Windows AppData Manager version")
if(NOT WAM_VERSION MATCHES
        "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?(\\+[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?$")
    message(FATAL_ERROR "WAM_VERSION must be a semantic version without a leading v")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Concurrent Gui Sql Widgets)
find_package(Qt${QT_VERSION_MAJOR}
    COMPONENTS
        Concurrent
        Gui
        Sql
        Widgets
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
    src/core/cleanup/CleanupPlanBuilder.cpp
    src/core/cleanup/CleanupPlanBuilder.h
    src/core/cleanup/CleanupValidator.cpp
    src/core/cleanup/CleanupValidator.h
    src/core/classifier/DataClassifier.cpp
    src/core/classifier/DataClassifier.h
    src/core/classifier/RiskAssessment.cpp
    src/core/classifier/RiskAssessment.h
    src/core/resolver/AppResolver.cpp
    src/core/resolver/AppResolver.h
    src/core/resolver/AttributionScorer.cpp
    src/core/resolver/AttributionScorer.h
    src/core/resolver/CandidateGenerator.cpp
    src/core/resolver/CandidateGenerator.h
    src/core/resolver/InstallationResolver.cpp
    src/core/resolver/InstallationResolver.h
    src/core/resolver/OrphanDetector.cpp
    src/core/resolver/OrphanDetector.h
    src/core/rules/IdentifierNormalization.h
    src/core/rules/GlobMatcher.cpp
    src/core/rules/GlobMatcher.h
    src/core/rules/RuleCatalog.cpp
    src/core/rules/RuleCatalog.h
    src/core/rules/RuleLoader.cpp
    src/core/rules/RuleLoader.h
    src/core/rules/RulePathResolver.cpp
    src/core/rules/RulePathResolver.h
    src/core/scanner/DirectoryScanner.cpp
    src/core/scanner/DirectoryScanner.h
    src/core/scanner/MetadataFingerprint.cpp
    src/core/scanner/MetadataFingerprint.h
    src/models/ApplicationInfo.h
    src/models/CleanupPlan.h
    src/models/InstallationEvidence.h
    src/models/RuleDefinition.h
    src/models/RuleMetadata.h
    src/models/ScanResult.h
    src/platform/windows/appx/AppxPackageCatalog.cpp
    src/platform/windows/appx/AppxPackageCatalog.h
    src/platform/windows/filesystem/AppDataPaths.cpp
    src/platform/windows/filesystem/AppDataPaths.h
    src/platform/windows/filesystem/DeletionAccessProbe.cpp
    src/platform/windows/filesystem/DeletionAccessProbe.h
    src/platform/windows/filesystem/ExecutableFileIdentity.cpp
    src/platform/windows/filesystem/ExecutableFileIdentity.h
    src/platform/windows/filesystem/ExecutableMetadataReader.cpp
    src/platform/windows/filesystem/ExecutableMetadataReader.h
    src/platform/windows/filesystem/PathPresenceReader.cpp
    src/platform/windows/filesystem/PathPresenceReader.h
    src/platform/windows/filesystem/ReparsePoint.cpp
    src/platform/windows/filesystem/ReparsePoint.h
    src/platform/windows/filesystem/StablePathIdentity.cpp
    src/platform/windows/filesystem/StablePathIdentity.h
    src/platform/windows/process/RunningProcessCatalog.cpp
    src/platform/windows/process/RunningProcessCatalog.h
    src/platform/windows/registry/InstalledApplicationRegistry.cpp
    src/platform/windows/registry/InstalledApplicationRegistry.h
    src/platform/windows/security/AuthenticodeVerifier.cpp
    src/platform/windows/security/AuthenticodeVerifier.h
    src/platform/windows/shell/RecycleBinExecutor.cpp
    src/platform/windows/shell/RecycleBinExecutor.h
    src/repositories/CleanupHistoryRepository.cpp
    src/repositories/CleanupHistoryRepository.h
    src/services/CleanupExecutor.h
    src/services/CleanupService.cpp
    src/services/CleanupService.h
    src/services/InstallationEvidenceCollector.cpp
    src/services/InstallationEvidenceCollector.h
    src/services/ScanService.cpp
    src/services/ScanService.h
    src/services/SettingsService.cpp
    src/services/SettingsService.h
)

add_library(wam_backend STATIC ${BACKEND_SOURCES})
target_link_libraries(wam_backend PUBLIC Qt::Core Qt::Concurrent Qt::Gui Qt::Sql Qt::Widgets)
if(MSVC)
    target_compile_options(wam_backend PRIVATE /EHsc)
endif()
if(WIN32)
    target_link_libraries(wam_backend PRIVATE
        windowsapp
        advapi32
        crypt32
        ole32
        shell32
        version
        wintrust
    )
endif()
target_include_directories(wam_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(QT_VERSION_MAJOR EQUAL 6)
    file(GLOB WAM_BUILTIN_RULE_FILES
        RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/rules/builtin/*.json"
    )
    list(SORT WAM_BUILTIN_RULE_FILES)

    qt_add_resources(wam_backend "wam_builtin_rules"
        PREFIX "/windowsappdatamanager/rules"
        BASE "${CMAKE_CURRENT_SOURCE_DIR}/rules"
        FILES
            rules/schema.json
            ${WAM_BUILTIN_RULE_FILES}
    )
endif()

set(PROJECT_SOURCES
    main.cpp
    src/platform/windows/appearance/WindowAppearance.cpp
    src/platform/windows/appearance/WindowAppearance.h
)

if(QT_VERSION VERSION_LESS 5.15)
    qt5_add_resources(PROJECT_SOURCES qml.qrc)
elseif(QT_VERSION VERSION_LESS 6.2)
    qt_add_resources(PROJECT_SOURCES qml.qrc)
endif()

qt_add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})
if(WIN32)
    target_sources(${PROJECT_NAME} PRIVATE
        "${CMAKE_SOURCE_DIR}/WindowsAppDataManager.rc"
    )
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE
    WAM_VERSION="${WAM_VERSION}"
)

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
            components/AutoScrollText.qml
            components/EmptyState.qml
            components/InlineNotice.qml
            components/ScanProgressStrip.qml
            components/ScanProgressBar.qml
            components/ScanRadar.qml
            components/ScanOverviewPanel.qml
            components/ScanActivityPanel.qml
            components/LiveScanPanel.qml
            components/ScanWorkspace.qml
            components/OverviewStats.qml
            components/DispositionSummary.qml
            components/OverviewApplicationList.qml
            components/IconButton.qml
            components/InsetStateLayer.qml
            components/NavItem.qml
            components/RiskBadge.qml
            components/StatTile.qml
            components/ThemedIcon.qml
            components/ApplicationIcon.qml
            components/ApplicationRow.qml
            components/FilterComboBox.qml
            components/FocusAwareFlickable.qml
            components/ApplicationFilterBar.qml
            components/ApplicationListHeader.qml
            components/ApplicationDetailContent.qml
            components/ConfidenceIndicator.qml
            components/CleanupItemRow.qml
            components/DataCategoryRow.qml
            components/DetailMetric.qml
            components/DetailPanel.qml
            components/EvidenceRow.qml
            components/PathField.qml
            components/PreferenceOption.qml
            pages/OverviewPage.qml
            pages/ApplicationsPage.qml
            pages/ApplicationDetailPage.qml
            pages/CleanupPage.qml
            pages/SettingsPage.qml
        SOURCES
            src/qmlmodels/AppBackend.cpp
            src/qmlmodels/AppBackend.h
            src/qmlmodels/ApplicationListModel.cpp
            src/qmlmodels/ApplicationListModel.h
            src/qmlmodels/CleanupPlanModel.cpp
            src/qmlmodels/CleanupPlanModel.h
            src/qmlmodels/CleanupViewModel.cpp
            src/qmlmodels/CleanupViewModel.h
            src/qmlmodels/ApplicationFilterModel.cpp
            src/qmlmodels/ApplicationFilterModel.h
            src/qmlmodels/ScanViewModel.cpp
            src/qmlmodels/ScanViewModel.h
            src/qmlmodels/SettingsViewModel.cpp
            src/qmlmodels/SettingsViewModel.h
        RESOURCES
            resources/Icons/IcBaselineCleaningServices.svg
            resources/Icons/TablerActivityHeartbeat.svg
            resources/Icons/TablerAlertSmall.svg
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
            resources/Icons/TablerTrash.svg
            resources/Icons/TablerTrashFilled.svg
            resources/Icons/TablerX.svg
            resources/Icons/TablerZoom.svg
            resources/Icons/TablerZoomScan.svg
    )

    # 这些头文件共同决定 QML 单例及其内嵌模型的对象布局。显式声明依赖，
    # 避免 MSVC/Ninja 增量构建复用旧布局的 AppBackend 或类型注册对象。
    set(WAM_QML_MODEL_ABI_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/qmlmodels/AppBackend.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/qmlmodels/ApplicationFilterModel.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/qmlmodels/ApplicationListModel.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/qmlmodels/CleanupPlanModel.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/qmlmodels/CleanupViewModel.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/qmlmodels/ScanViewModel.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/qmlmodels/SettingsViewModel.h"
    )
    set_property(SOURCE src/qmlmodels/AppBackend.cpp APPEND PROPERTY
        OBJECT_DEPENDS ${WAM_QML_MODEL_ABI_HEADERS}
    )
    set_property(
        SOURCE "${CMAKE_CURRENT_BINARY_DIR}/windowsappdatamanager_qmltyperegistrations.cpp"
        APPEND PROPERTY OBJECT_DEPENDS ${WAM_QML_MODEL_ABI_HEADERS}
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
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE dwmapi)
endif()

if(BUILD_TESTING)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

    add_test(NAME wam_qml_startup_test
        COMMAND $<TARGET_FILE:${PROJECT_NAME}> --startup-check
    )
    set_tests_properties(wam_qml_startup_test PROPERTIES
        TIMEOUT 20
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;WAM_STARTUP_CHECK=1"
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>;QT_PLUGIN_PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>/../plugins"
    )

    add_executable(wam_backend_tests
        tests/tst_backend.cpp
    )
    target_link_libraries(wam_backend_tests PRIVATE wam_backend Qt::Test)
    add_test(NAME wam_backend_tests COMMAND wam_backend_tests)
    set_tests_properties(wam_backend_tests PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>"
    )

    add_executable(wam_qml_models_tests
        tests/tst_qml_models.cpp
        src/qmlmodels/ApplicationListModel.cpp
        src/qmlmodels/ApplicationListModel.h
        src/qmlmodels/ApplicationFilterModel.cpp
        src/qmlmodels/ApplicationFilterModel.h
        src/qmlmodels/ScanViewModel.cpp
        src/qmlmodels/ScanViewModel.h
    )
    target_link_libraries(wam_qml_models_tests PRIVATE
        wam_backend
        Qt::Gui
        Qt::Qml
        Qt::Test
    )
    add_test(NAME wam_qml_models_tests COMMAND wam_qml_models_tests)
    set_tests_properties(wam_qml_models_tests PROPERTIES
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

    add_executable(wam_executable_evidence_tests
        tests/tst_executable_evidence.cpp
    )
    target_link_libraries(wam_executable_evidence_tests PRIVATE wam_backend Qt::Test)
    add_test(NAME wam_executable_evidence_tests COMMAND wam_executable_evidence_tests)
    set_tests_properties(wam_executable_evidence_tests PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>"
    )

    add_executable(wam_executable_platform_tests
        tests/tst_executable_platform.cpp
    )
    target_link_libraries(wam_executable_platform_tests PRIVATE wam_backend Qt::Test)
    add_test(NAME wam_executable_platform_tests COMMAND wam_executable_platform_tests)
    set_tests_properties(wam_executable_platform_tests PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>"
    )

    add_executable(wam_orphan_detection_tests
        tests/tst_orphan_detection.cpp
    )
    target_link_libraries(wam_orphan_detection_tests PRIVATE wam_backend Qt::Test)
    add_test(NAME wam_orphan_detection_tests COMMAND wam_orphan_detection_tests)
    set_tests_properties(wam_orphan_detection_tests PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>"
    )

    add_executable(wam_cleanup_tests
        tests/tst_cleanup.cpp
        src/qmlmodels/CleanupPlanModel.cpp
        src/qmlmodels/CleanupPlanModel.h
    )
    target_link_libraries(wam_cleanup_tests PRIVATE
        wam_backend
        Qt::Qml
        Qt::Test
    )
    add_test(NAME wam_cleanup_tests COMMAND wam_cleanup_tests)
    set_tests_properties(wam_cleanup_tests PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>;QT_PLUGIN_PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>/../plugins"
    )

    add_executable(wam_cleanup_safety_tests
        tests/tst_cleanup_safety.cpp
    )
    target_link_libraries(wam_cleanup_safety_tests PRIVATE
        wam_backend
        Qt::Test
    )
    add_test(NAME wam_cleanup_safety_tests COMMAND wam_cleanup_safety_tests)
    set_tests_properties(wam_cleanup_safety_tests PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>;QT_PLUGIN_PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>/../plugins"
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
