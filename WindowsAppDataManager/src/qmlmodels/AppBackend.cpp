#include "AppBackend.h"

namespace wam::qmlmodels {

AppBackend::AppBackend(QObject *parent)
    : QObject(parent),
      m_applicationFilter(&m_applications),
      m_scan(&m_applications)
{
}

ApplicationListModel *AppBackend::applications()
{
    return &m_applications;
}

ApplicationFilterModel *AppBackend::applicationFilter()
{
    return &m_applicationFilter;
}

ScanViewModel *AppBackend::scan()
{
    return &m_scan;
}

SettingsViewModel *AppBackend::settings()
{
    return &m_settings;
}

} // namespace wam::qmlmodels
