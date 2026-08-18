#include "AppBackend.h"

namespace wam::qmlmodels {

AppBackend::AppBackend(QObject *parent)
    : QObject(parent),
      m_applicationFilter(&m_applications),
      m_scan(&m_applications)
{
    connect(&m_scan, &ScanViewModel::scanResultAccepted,
            &m_cleanup, &CleanupViewModel::setScanResult);
    connect(&m_cleanup, &CleanupViewModel::rescanRequested,
            &m_scan, &ScanViewModel::startScan);
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

CleanupViewModel *AppBackend::cleanup()
{
    return &m_cleanup;
}

SettingsViewModel *AppBackend::settings()
{
    return &m_settings;
}

} // namespace wam::qmlmodels
