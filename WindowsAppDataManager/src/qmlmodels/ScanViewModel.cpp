#include "ScanViewModel.h"

#include <QDateTime>
#include <QDir>

#include <algorithm>

namespace wam::qmlmodels {

ScanViewModel::ScanViewModel(ApplicationListModel *applicationModel, QObject *parent)
    : QObject(parent),
      m_applicationModel(applicationModel),
      m_service(this)
{
    Q_ASSERT(m_applicationModel);

    connect(&m_service, &services::ScanService::targetPathChanged,
            this, &ScanViewModel::targetPathChanged);
    connect(&m_service, &services::ScanService::scanStarted, this, [this] {
        setRunning(true);
        setProgress(0);
        setCurrentPath({});
        setStatusText(QStringLiteral("正在分析 AppData"));
        m_issueCount = 0;
        emit issueCountChanged();
        clearError();
    });
    connect(&m_service, &services::ScanService::progressChanged,
            this, [this](int value, const QString &path) {
        setProgress(value);
        setCurrentPath(QDir::toNativeSeparators(path));
    });
    connect(&m_service, &services::ScanService::scanCompleted,
            this, [this](const ScanResult &result) {
        setRunning(false);
        setCurrentPath({});
        m_issueCount = result.issues.size();
        emit issueCountChanged();
        if (result.cancelled) {
            setProgress(0);
            setStatusText(QStringLiteral("扫描已取消，保留上一次完整结果"));
            return;
        }

        m_applicationModel->setApplications(result.applications);
        setProgress(100);
        m_lastScanText = QStringLiteral("今天 %1").arg(
                QDateTime::currentDateTime().time().toString(QStringLiteral("HH:mm")));
        emit lastScanTextChanged();
        setStatusText(m_issueCount > 0
                      ? QStringLiteral("扫描完成，%1 个位置未能完整读取").arg(m_issueCount)
                      : QStringLiteral("扫描结果已就绪"));
    });
    connect(&m_service, &services::ScanService::scanFailed,
            this, [this](const QString &message, const QString &detail) {
        setRunning(false);
        setCurrentPath({});
        setStatusText(message);
        m_errorMessage = message;
        m_technicalDetail = detail;
        emit errorChanged();
    });
}

bool ScanViewModel::running() const { return m_running; }
int ScanViewModel::progress() const { return m_progress; }
QString ScanViewModel::currentPath() const { return m_currentPath; }
QString ScanViewModel::targetPath() const { return m_service.targetPath(); }
QString ScanViewModel::statusText() const { return m_statusText; }
QString ScanViewModel::lastScanText() const { return m_lastScanText; }
QString ScanViewModel::errorMessage() const { return m_errorMessage; }
QString ScanViewModel::technicalDetail() const { return m_technicalDetail; }
int ScanViewModel::issueCount() const { return m_issueCount; }
bool ScanViewModel::partialResult() const { return m_issueCount > 0; }

void ScanViewModel::toggleScan()
{
    if (m_running)
        cancelScan();
    else
        startScan();
}

void ScanViewModel::startScan()
{
    if (m_running)
        return;
    m_service.startScan();
}

void ScanViewModel::cancelScan()
{
    if (!m_running)
        return;
    setStatusText(QStringLiteral("正在停止扫描…"));
    m_service.cancelScan();
}

void ScanViewModel::setRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    emit runningChanged();
}

void ScanViewModel::setProgress(int progress)
{
    progress = std::clamp(progress, 0, 100);
    if (m_progress == progress)
        return;
    m_progress = progress;
    emit progressChanged();
}

void ScanViewModel::setCurrentPath(QString path)
{
    if (m_currentPath == path)
        return;
    m_currentPath = std::move(path);
    emit currentPathChanged();
}

void ScanViewModel::setStatusText(QString status)
{
    if (m_statusText == status)
        return;
    m_statusText = std::move(status);
    emit statusTextChanged();
}

void ScanViewModel::clearError()
{
    if (m_errorMessage.isEmpty() && m_technicalDetail.isEmpty())
        return;
    m_errorMessage.clear();
    m_technicalDetail.clear();
    emit errorChanged();
}

} // namespace wam::qmlmodels
