#pragma once

#include "../services/SettingsService.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace wam::qmlmodels {

class SettingsViewModel : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(SettingsViewModel)
    QML_UNCREATABLE("由 Backend 提供")
    Q_PROPERTY(int themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    Q_PROPERTY(int motionPreference READ motionPreference WRITE setMotionPreference
               NOTIFY motionPreferenceChanged)

public:
    explicit SettingsViewModel(QObject *parent = nullptr);

    [[nodiscard]] int themeMode() const;
    [[nodiscard]] int motionPreference() const;

    void setThemeMode(int mode);
    void setMotionPreference(int preference);

signals:
    void themeModeChanged();
    void motionPreferenceChanged();

private:
    static bool isValidPreference(int value);

    services::SettingsService m_service;
    int m_themeMode = 0;
    int m_motionPreference = 0;
};

} // namespace wam::qmlmodels
