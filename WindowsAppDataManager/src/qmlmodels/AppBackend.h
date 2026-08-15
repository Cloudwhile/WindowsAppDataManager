#pragma once

#include "ApplicationListModel.h"
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
    Q_PROPERTY(wam::qmlmodels::ScanViewModel *scan READ scan CONSTANT)
    Q_PROPERTY(wam::qmlmodels::SettingsViewModel *settings READ settings CONSTANT)

public:
    explicit AppBackend(QObject *parent = nullptr);

    [[nodiscard]] ApplicationListModel *applications();
    [[nodiscard]] ScanViewModel *scan();
    [[nodiscard]] SettingsViewModel *settings();

private:
    ApplicationListModel m_applications;
    ScanViewModel m_scan;
    SettingsViewModel m_settings;
};

} // namespace wam::qmlmodels
