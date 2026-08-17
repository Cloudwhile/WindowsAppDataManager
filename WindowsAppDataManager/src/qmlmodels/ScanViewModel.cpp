#include "ScanViewModel.h"

#include "../services/CleanupPlanBuilder.h"
#include "../services/ScanReportExporter.h"

#include <QDateTime>
#include <QDir>
#include <QLocale>
#include <QVariantMap>

#include <algorithm>

namespace wam::qmlmodels {
namespace {

QString issueCodeText(ScanErrorCode code)
{
    switch (code) {
    case ScanErrorCode::AccessDenied:
        return QStringLiteral("访问被拒绝");
    case ScanErrorCode::PathUnavailable:
        return QStringLiteral("路径不可用");
    case ScanErrorCode::IoError:
        return QStringLiteral("I/O 错误");
    case ScanErrorCode::Cancelled:
        return QStringLiteral("扫描已取消");
    }
    return QStringLiteral("未知错误");
}

QString formatSize(quint64 bytes)
{
    static constexpr quint64 kibibyte = 1024;
    static constexpr quint64 mebibyte = kibibyte * 1024;
    static constexpr quint64 gibibyte = mebibyte * 1024;
    const QLocale locale;

    if (bytes >= gibibyte)
        return locale.toString(static_cast<double>(bytes) / gibibyte, 'f', 1) + QStringLiteral(" GB");
    if (bytes >= mebibyte)
        return locale.toString(static_cast<double>(bytes) / mebibyte, 'f', 1) + QStringLiteral(" MB");
    if (bytes >= kibibyte)
        return locale.toString(static_cast<double>(bytes) / kibibyte, 'f', 1) + QStringLiteral(" KB");
    return locale.toString(bytes) + QStringLiteral(" B");
}

QString categoryText(DataCategory category)
{
    switch (category) {
    case DataCategory::Cache: return QStringLiteral("缓存");
    case DataCategory::Log: return QStringLiteral("日志");
    case DataCategory::Temp: return QStringLiteral("临时数据");
    case DataCategory::CrashDump: return QStringLiteral("崩溃报告");
    case DataCategory::Config: return QStringLiteral("配置");
    case DataCategory::Database: return QStringLiteral("数据库");
    case DataCategory::Session: return QStringLiteral("会话数据");
    case DataCategory::Cookie: return QStringLiteral("Cookie");
    case DataCategory::Credential: return QStringLiteral("凭据");
    case DataCategory::UserData: return QStringLiteral("用户数据");
    case DataCategory::Workspace: return QStringLiteral("工作区");
    case DataCategory::SaveGame: return QStringLiteral("存档");
    case DataCategory::DownloadedResource: return QStringLiteral("下载资源");
    case DataCategory::Extension: return QStringLiteral("扩展数据");
    case DataCategory::Unknown: return QStringLiteral("无法判断");
    }
    return QStringLiteral("无法判断");
}

QString riskText(RiskLevel risk)
{
    switch (risk) {
    case RiskLevel::Safe: return QStringLiteral("安全");
    case RiskLevel::Low: return QStringLiteral("低风险");
    case RiskLevel::Caution: return QStringLiteral("需确认");
    case RiskLevel::High: return QStringLiteral("高风险");
    case RiskLevel::Protected: return QStringLiteral("受保护");
    case RiskLevel::Unknown: return QStringLiteral("无法判断");
    }
    return QStringLiteral("无法判断");
}

} // namespace

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
        if (result.cancelled) {
            setProgress(0);
            setStatusText(QStringLiteral("扫描已取消，保留上一次完整结果"));
            return;
        }

        m_applicationModel->setApplications(result.applications);
        clearCleanupPlan();
        m_issues = result.issues;
        emit issuesChanged();
        m_issueCount = result.issues.size();
        emit issueCountChanged();
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

QVariantList ScanViewModel::issues() const
{
    QVariantList rows;
    rows.reserve(m_issues.size());
    for (const ScanIssue &issue : m_issues) {
        QVariantMap row;
        row.insert(QStringLiteral("message"), issue.message);
        row.insert(QStringLiteral("technicalDetail"), issue.technicalDetail);
        row.insert(QStringLiteral("path"), issue.path);
        row.insert(QStringLiteral("code"), static_cast<int>(issue.code));
        row.insert(QStringLiteral("codeText"), issueCodeText(issue.code));
        rows.append(row);
    }
    return rows;
}

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

QString ScanViewModel::exportReport(const QUrl &destination) const
{
    return services::exportScanReport(destination, m_applicationModel->snapshot(), m_issues);
}

QVariantList ScanViewModel::cleanupPlan() const
{
    return m_cleanupPlan;
}

QString ScanViewModel::cleanupPlanTotalText() const
{
    return formatSize(m_cleanupPlanTotalSize);
}

void ScanViewModel::generateCleanupPlan()
{
    const services::CleanupPlan plan = services::buildCleanupPlan(m_applicationModel->snapshot());
    QVariantList rows;
    rows.reserve(plan.items.size());
    for (const services::CleanupPlanItem &item : plan.items) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), item.id);
        row.insert(QStringLiteral("applicationId"), item.applicationId);
        row.insert(QStringLiteral("applicationName"), item.applicationName);
        row.insert(QStringLiteral("categoryText"), categoryText(item.category));
        row.insert(QStringLiteral("path"), item.path);
        row.insert(QStringLiteral("impact"), item.impact);
        row.insert(QStringLiteral("ruleSource"), item.ruleSource);
        row.insert(QStringLiteral("sizeBytes"), QVariant::fromValue<qulonglong>(item.size));
        row.insert(QStringLiteral("sizeText"), formatSize(item.size));
        row.insert(QStringLiteral("fileCount"), QVariant::fromValue<qulonglong>(item.fileCount));
        row.insert(QStringLiteral("fileCountText"), QLocale().toString(item.fileCount));
        row.insert(QStringLiteral("riskLevel"), static_cast<int>(item.risk));
        row.insert(QStringLiteral("riskText"), riskText(item.risk));
        rows.append(row);
    }

    m_cleanupPlan = std::move(rows);
    m_cleanupPlanTotalSize = plan.totalSize;
    emit cleanupPlanChanged();
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

void ScanViewModel::clearCleanupPlan()
{
    if (m_cleanupPlan.isEmpty() && m_cleanupPlanTotalSize == 0)
        return;
    m_cleanupPlan.clear();
    m_cleanupPlanTotalSize = 0;
    emit cleanupPlanChanged();
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
