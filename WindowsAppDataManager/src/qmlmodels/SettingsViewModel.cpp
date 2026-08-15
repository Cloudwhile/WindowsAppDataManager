#include "SettingsViewModel.h"

namespace wam::qmlmodels {

SettingsViewModel::SettingsViewModel(QObject *parent)
    : QObject(parent),
      m_themeMode(m_service.themeMode()),
      m_motionPreference(m_service.motionPreference())
{
}

int SettingsViewModel::themeMode() const
{
    return m_themeMode;
}

int SettingsViewModel::motionPreference() const
{
    return m_motionPreference;
}

void SettingsViewModel::setThemeMode(int mode)
{
    if (!isValidPreference(mode) || m_themeMode == mode)
        return;

    m_themeMode = mode;
    m_service.setThemeMode(mode);
    emit themeModeChanged();
}

void SettingsViewModel::setMotionPreference(int preference)
{
    if (!isValidPreference(preference) || m_motionPreference == preference)
        return;

    m_motionPreference = preference;
    m_service.setMotionPreference(preference);
    emit motionPreferenceChanged();
}

bool SettingsViewModel::isValidPreference(int value)
{
    return value >= 0 && value <= 2;
}

} // namespace wam::qmlmodels
