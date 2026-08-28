#include "ScanViewModel.h"

#include <QDateTime>
#include <QDir>

#include <algorithm>
#include <utility>

namespace wam::qmlmodels {
namespace {

constexpr int applicationUpdatesPerFrame = 24;
constexpr int applicationUpdateIntervalMs = 16;

} // namespace

ScanViewModel::ScanViewModel(ApplicationListModel *applicationModel, QObject *parent)
    : QObject(parent),
      m_applicationModel(applicationModel),
      m_service(this)
{
    Q_ASSERT(m_applicationModel);

    m_applicationUpdateTimer.setSingleShot(true);
    m_applicationUpdateTimer.setInterval(applicationUpdateIntervalMs);
    connect(&m_applicationUpdateTimer, &QTimer::timeout,
            this, &ScanViewModel::processPendingApplicationUpdates);

    connect(&m_service, &services::ScanService::targetPathChanged,
            this, &ScanViewModel::targetPathChanged);
    connect(&m_service, &services::ScanService::scanStarted, this, [this] {
        clearPendingApplicationUpdates();
        m_preScanApplications = m_applicationModel->applications();
        m_preScanIssueCount = m_acceptedIssueCount;
        m_scanTransactionActive = true;
        m_applicationModel->clear();
        setRunning(true);
        setProgress(0);
        setCurrentPath({});
        if (!m_recentPaths.isEmpty()) {
            m_recentPaths.clear();
            emit recentPathsChanged();
        }
        setStatusText(QStringLiteral("正在分析 AppData"));
        setIssueCount(0);
        clearError();
    });
    connect(&m_service, &services::ScanService::progressChanged,
            this, [this](int value, const QString &path) {
        // 路径更新和批量结果更新来自两条队列；快速扫描时到达顺序可能交错。
        // 扫描期间只接受向前的进度，避免数字进度短暂回跳。
        setProgress(std::max(m_progress, value));
        setCurrentPath(QDir::toNativeSeparators(path));
    });
    connect(&m_service, &services::ScanService::scanUpdatesReady,
            this, [this](const QVector<ApplicationInfo> &applications,
                         int issueCount,
                         int completedTargets,
                         int totalTargets) {
        if (!m_scanTransactionActive || !m_running)
            return;

        enqueueApplicationUpdates(applications);
        setIssueCount(issueCount);
        if (totalTargets > 0) {
            setProgress(std::max(
                    m_progress, completedTargets * 100 / totalTargets));
            setStatusText(QStringLiteral(
                    "已完成 %1 / %2 个扫描单位，继续分析…")
                                  .arg(completedTargets)
                                  .arg(totalTargets));
        }
        if (!m_applicationUpdateTimer.isActive())
            processPendingApplicationUpdates();
    });
    connect(&m_service, &services::ScanService::scanCompleted,
            this, [this](const ScanResult &result) {
        setCurrentPath({});
        if (result.cancelled) {
            clearPendingApplicationUpdates();
            setRunning(false);
            setProgress(0);
            restorePreScanSnapshot();
            setStatusText(QStringLiteral("扫描已取消，保留上一次完整结果"));
            return;
        }

        // Replace any not-yet-published intermediate values with the
        // authoritative result, then finish in small UI-sized batches.
        m_pendingApplications.clear();
        enqueueApplicationUpdates(result.applications);
        m_pendingScanResult = result;
        setProgress(100);
        setStatusText(QStringLiteral("正在整理扫描结果…"));
        if (!m_applicationUpdateTimer.isActive())
            processPendingApplicationUpdates();
    });
    connect(&m_service, &services::ScanService::scanFailed,
            this, [this](const QString &message, const QString &detail) {
        clearPendingApplicationUpdates();
        setRunning(false);
        setCurrentPath({});
        restorePreScanSnapshot();
        setStatusText(message);
        m_errorMessage = message;
        m_technicalDetail = detail;
        emit errorChanged();
    });
}

bool ScanViewModel::running() const { return m_running; }
int ScanViewModel::progress() const { return m_progress; }
QString ScanViewModel::currentPath() const { return m_currentPath; }
QStringList ScanViewModel::recentPaths() const { return m_recentPaths; }
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

    if (m_currentPath.isEmpty())
        return;

    m_recentPaths.removeAll(m_currentPath);
    m_recentPaths.prepend(m_currentPath);
    while (m_recentPaths.size() > 5)
        m_recentPaths.removeLast();
    emit recentPathsChanged();
}

void ScanViewModel::setStatusText(QString status)
{
    if (m_statusText == status)
        return;
    m_statusText = std::move(status);
    emit statusTextChanged();
}

void ScanViewModel::setIssueCount(int issueCount)
{
    issueCount = std::max(0, issueCount);
    if (m_issueCount == issueCount)
        return;
    m_issueCount = issueCount;
    emit issueCountChanged();
}

void ScanViewModel::enqueueApplicationUpdates(
        const QVector<ApplicationInfo> &applications)
{
    for (const ApplicationInfo &application : applications)
        m_pendingApplications.insert(application.id, application);
}

void ScanViewModel::processPendingApplicationUpdates()
{
    if (!m_scanTransactionActive || !m_running) {
        clearPendingApplicationUpdates();
        return;
    }

    QVector<ApplicationInfo> batch;
    const int batchSize = std::min(
            applicationUpdatesPerFrame,
            static_cast<int>(m_pendingApplications.size()));
    batch.reserve(batchSize);
    auto iterator = m_pendingApplications.begin();
    while (iterator != m_pendingApplications.end()
           && batch.size() < batchSize) {
        batch.append(iterator.value());
        iterator = m_pendingApplications.erase(iterator);
    }

    if (!batch.isEmpty())
        m_applicationModel->mergeScanUpdates(std::move(batch));

    if (!m_pendingApplications.isEmpty()) {
        m_applicationUpdateTimer.start();
        return;
    }

    if (m_pendingScanResult.has_value())
        finishAcceptedScan();
}

void ScanViewModel::finishAcceptedScan()
{
    Q_ASSERT(m_pendingScanResult.has_value());
    ScanResult result = std::move(*m_pendingScanResult);
    m_pendingScanResult.reset();

    emit scanResultAccepted(result);
    m_acceptedIssueCount = result.issues.size();
    setIssueCount(m_acceptedIssueCount);
    finishScanTransaction();
    setProgress(100);
    m_lastScanText = QStringLiteral("今天 %1").arg(
            QDateTime::currentDateTime().time().toString(QStringLiteral("HH:mm")));
    emit lastScanTextChanged();
    setRunning(false);
    setStatusText(m_issueCount > 0
                  ? QStringLiteral("扫描完成，%1 个位置未能完整读取").arg(m_issueCount)
                  : QStringLiteral("扫描结果已就绪"));
}

void ScanViewModel::clearPendingApplicationUpdates()
{
    m_applicationUpdateTimer.stop();
    m_pendingApplications.clear();
    m_pendingScanResult.reset();
}

void ScanViewModel::restorePreScanSnapshot()
{
    if (!m_scanTransactionActive) {
        setIssueCount(m_acceptedIssueCount);
        return;
    }

    m_applicationModel->setApplications(std::move(m_preScanApplications));
    setIssueCount(m_preScanIssueCount);
    finishScanTransaction();
}

void ScanViewModel::finishScanTransaction()
{
    m_preScanApplications.clear();
    m_preScanIssueCount = 0;
    m_scanTransactionActive = false;
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
