#pragma once

#include "ApplicationListModel.h"
#include "ApplicationFilterModel.h"
#include "CleanupViewModel.h"
#include "ScanViewModel.h"
#include "SettingsViewModel.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace wam::qmlmodels {

class AppBackend : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Backend)
    QML_SINGLETON
    Q_PROPERTY(wam::qmlmodels::ApplicationListModel *applications READ applications CONSTANT)
    Q_PROPERTY(wam::qmlmodels::ApplicationFilterModel *applicationFilter
               READ applicationFilter CONSTANT)
    Q_PROPERTY(wam::qmlmodels::ScanViewModel *scan READ scan CONSTANT)
    Q_PROPERTY(wam::qmlmodels::CleanupViewModel *cleanup READ cleanup CONSTANT)
    Q_PROPERTY(wam::qmlmodels::SettingsViewModel *settings READ settings CONSTANT)

public:
    explicit AppBackend(QObject *parent = nullptr);

    [[nodiscard]] ApplicationListModel *applications();
    [[nodiscard]] ApplicationFilterModel *applicationFilter();
    [[nodiscard]] ScanViewModel *scan();
    [[nodiscard]] CleanupViewModel *cleanup();
    [[nodiscard]] SettingsViewModel *settings();

private:
    ApplicationListModel m_applications;
    ApplicationFilterModel m_applicationFilter;
    ScanViewModel m_scan;
    CleanupViewModel m_cleanup;
    SettingsViewModel m_settings;
};

} // namespace wam::qmlmodels
