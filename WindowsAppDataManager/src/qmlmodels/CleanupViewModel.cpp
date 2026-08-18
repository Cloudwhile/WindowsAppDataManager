#include "CleanupViewModel.h"

#include "../core/cleanup/CleanupPlanBuilder.h"
#include "../repositories/CleanupHistoryRepository.h"

#include <QLocale>

namespace wam::qmlmodels {
namespace {

QString formatSize(quint64 bytes)
{
    static const QStringList units {
        QStringLiteral("B"), QStringLiteral("KB"), QStringLiteral("MB"),
        QStringLiteral("GB"), QStringLiteral("TB")
    };
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < units.size() - 1) {
        value /= 1024.0;
        ++unit;
    }
    const int decimals = unit == 0 || value >= 100.0 ? 0 : value >= 10.0 ? 1 : 2;
    return QStringLiteral("%1 %2").arg(
            QLocale().toString(value, 'f', decimals), units.at(unit));
}

} // namespace

CleanupViewModel::CleanupViewModel(QObject *parent)
    : QObject(parent),
      m_items(this),
      m_service(this)
{
    connect(&m_items, &CleanupPlanModel::summaryChanged,
            this, &CleanupViewModel::planChanged);
    connect(&m_service, &services::CleanupService::cleanupStarted,
            this, [this](const QString &) {
        clearError();
        setRunning(true);
        setStatusText(QStringLiteral("正在验证所选清理项目"));
    });
    connect(&m_service, &services::CleanupService::itemChanged,
            this, [this](int index, CleanupItemState state,
                         const QString &message, quint64 releasedSize) {
        m_items.updateItem(index, state, message, releasedSize);
        setStatusText(message);
    });
    connect(&m_service, &services::CleanupService::cleanupCompleted,
            this, [this](const CleanupRunResult &result) {
        setRunning(false);
        m_items.setPlan(result.plan);
        setStatusText(result.cancelled
                      ? QStringLiteral("清理已取消，已完成项目保持记录")
                      : result.history.failureCount > 0
                        ? QStringLiteral("清理完成，部分项目因安全检查未处理")
                        : QStringLiteral("清理完成，已将 %1 移入回收站")
                                  .arg(formatSize(result.history.releasedSize)));
        refreshHistory();
        if (result.filesystemOperationAttempted)
            emit rescanRequested();
    });
    connect(&m_service, &services::CleanupService::cleanupFailed,
            this, [this](const QString &message, const QString &detail) {
        setRunning(false);
        setStatusText(message);
        m_errorMessage = message;
        m_technicalDetail = detail;
        emit errorChanged();
        refreshHistory();
        emit rescanRequested();
    });
    refreshHistory();
}

CleanupPlanModel *CleanupViewModel::items() { return &m_items; }
bool CleanupViewModel::running() const { return m_running; }
bool CleanupViewModel::hasScan() const { return m_hasScan; }
bool CleanupViewModel::hasPlan() const { return m_items.count() > 0; }
bool CleanupViewModel::canExecute() const
{
    if (!m_hasScan || m_running)
        return false;
    for (const CleanupPlanItem &item : m_items.plan().items) {
        if (item.selected && item.state == CleanupItemState::Pending)
            return true;
    }
    return false;
}
QString CleanupViewModel::statusText() const { return m_statusText; }
QString CleanupViewModel::errorMessage() const { return m_errorMessage; }
QString CleanupViewModel::technicalDetail() const { return m_technicalDetail; }
QString CleanupViewModel::lastCleanupText() const { return m_lastCleanupText; }
QString CleanupViewModel::lastReleasedSizeText() const { return m_lastReleasedSizeText; }

void CleanupViewModel::setScanResult(const ScanResult &result)
{
    if (result.cancelled)
        return;
    m_applications = result.applications;
    m_scanRoots = result.roots;
    m_hasScan = true;
    emit planChanged();
    rebuildPlan();
}

void CleanupViewModel::rebuildPlan()
{
    if (m_running)
        return;
    clearError();
    if (!m_hasScan) {
        m_items.setPlan({});
        setStatusText(QStringLiteral("完成扫描后可生成安全清理计划"));
        return;
    }

    core::CleanupPlanBuildContext context;
    context.scanRoots = m_scanRoots;
    m_items.setPlan(core::CleanupPlanBuilder::build(m_applications, context));
    if (m_items.count() > 0) {
        setStatusText(QStringLiteral("已生成 %1 项安全清理候选")
                              .arg(m_items.count()));
    } else if (m_items.excludedCount() > 0) {
        setStatusText(QStringLiteral("当前项目均未通过保守清理条件"));
    } else {
        setStatusText(QStringLiteral("本次扫描没有已验证的安全清理候选"));
    }
    emit planChanged();
}

void CleanupViewModel::executeSelected()
{
    if (!canExecute())
        return;
    m_service.execute(m_items.plan(), m_scanRoots);
}

void CleanupViewModel::cancel()
{
    if (!m_running)
        return;
    setStatusText(QStringLiteral("正在停止清理…"));
    m_service.cancel();
}

void CleanupViewModel::setRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    emit runningChanged();
    emit planChanged();
}

void CleanupViewModel::setStatusText(QString status)
{
    if (m_statusText == status)
        return;
    m_statusText = std::move(status);
    emit statusChanged();
}

void CleanupViewModel::clearError()
{
    if (m_errorMessage.isEmpty() && m_technicalDetail.isEmpty())
        return;
    m_errorMessage.clear();
    m_technicalDetail.clear();
    emit errorChanged();
}

void CleanupViewModel::refreshHistory()
{
    repositories::CleanupHistoryRepository repository;
    QString error;
    const QVector<CleanupHistoryRecord> history = repository.recentRuns(1, &error);
    if (history.isEmpty()) {
        if (!error.isEmpty() && m_errorMessage.isEmpty()) {
            m_errorMessage = QStringLiteral("无法读取清理历史");
            m_technicalDetail = error;
            emit errorChanged();
        }
        return;
    }

    const CleanupHistoryRecord &latest = history.constFirst();
    m_lastCleanupText = latest.completedAt.isValid()
            ? latest.completedAt.toLocalTime().toString(
                    QStringLiteral("M月d日 HH:mm"))
            : QStringLiteral("清理尚未完成");
    m_lastReleasedSizeText = formatSize(latest.releasedSize);
    emit historyChanged();
}

} // namespace wam::qmlmodels
