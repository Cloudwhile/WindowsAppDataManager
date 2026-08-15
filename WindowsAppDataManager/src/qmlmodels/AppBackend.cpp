#include "AppBackend.h"

namespace wam::qmlmodels {

AppBackend::AppBackend(QObject *parent)
    : QObject(parent),
      m_scan(&m_applications)
{
}

ApplicationListModel *AppBackend::applications()
{
    return &m_applications;
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
