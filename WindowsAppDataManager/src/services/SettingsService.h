#pragma once

#include <QSettings>

namespace wam::services {

class SettingsService final {
public:
    SettingsService();

    [[nodiscard]] int themeMode() const;
    [[nodiscard]] int motionPreference() const;

    void setThemeMode(int mode);
    void setMotionPreference(int preference);

private:
    QSettings m_settings;
};

} // namespace wam::services
