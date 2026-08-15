#include "SettingsService.h"

#include <algorithm>

namespace wam::services {
namespace {

constexpr int minimumPreference = 0;
constexpr int maximumPreference = 2;

int normalizedPreference(int value)
{
    return std::clamp(value, minimumPreference, maximumPreference);
}

} // namespace

SettingsService::SettingsService() = default;

int SettingsService::themeMode() const
{
    return normalizedPreference(
            m_settings.value(QStringLiteral("appearance/themeMode"), 0).toInt());
}

int SettingsService::motionPreference() const
{
    return normalizedPreference(
            m_settings.value(QStringLiteral("appearance/motionPreference"), 0).toInt());
}

void SettingsService::setThemeMode(int mode)
{
    m_settings.setValue(QStringLiteral("appearance/themeMode"), normalizedPreference(mode));
    m_settings.sync();
}

void SettingsService::setMotionPreference(int preference)
{
    m_settings.setValue(QStringLiteral("appearance/motionPreference"),
                        normalizedPreference(preference));
    m_settings.sync();
}

} // namespace wam::services
