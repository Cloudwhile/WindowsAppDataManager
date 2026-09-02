#include "CleanupViewModel.h"

#include "../core/cleanup/CleanupPlanBuilder.h"
#include "../repositories/CleanupHistoryRepository.h"

#include <QLocale>

#include <algorithm>

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

int selectedOrdinal(const CleanupPlan &plan, int index)
{
    int ordinal = 0;
    const int itemCount = static_cast<int>(plan.items.size());
    const int last = std::min(index, itemCount - 1);
    for (int current = 0; current <= last; ++current) {
        if (plan.items.at(current).selected)
            ++ordinal;
    }
    return ordinal;
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
        setResultVisible(false);
        setCancelling(false);
        setRunning(true);
        setStatusText(QStringLiteral("正在验证所选清理项目"));
    });
    connect(&m_service, &services::CleanupService::itemChanged,
            this, [this](int index, CleanupItemState state,
                         const QString &message, quint64 releasedSize) {
        if (!m_running)
            return;
        m_items.updateItem(index, state, message, releasedSize);
        const int total = m_items.selectedCount();
        const int ordinal = selectedOrdinal(m_items.plan(), index);
        const bool currentSelected = index >= 0
                && index < m_items.plan().items.size()
                && m_items.plan().items.at(index).selected;
        if (currentSelected && ordinal > 0 && total > 0) {
            setStatusText(QStringLiteral("正在处理 %1 / %2：%3")
                                  .arg(ordinal)
                                  .arg(total)
                                  .arg(message));
        } else {
            setStatusText(message);
        }
    });
    connect(&m_service, &services::CleanupService::cleanupCompleted,
            this, [this](const CleanupRunResult &result) {
        m_items.setPlan(result.plan);
        setResultVisible(true);
        setCancelling(false);
        if (result.cancelled) {
            setStatusText(QStringLiteral("清理已停止：完成 %1 项，跳过 %2 项，释放 %3")
                                  .arg(result.history.successCount)
                                  .arg(m_items.skippedCount())
                                  .arg(formatSize(result.history.releasedSize)));
        } else if (result.history.failureCount > 0) {
            setStatusText(QStringLiteral("清理完成：成功 %1 项，未处理 %2 项，释放 %3")
                                  .arg(result.history.successCount)
                                  .arg(result.history.failureCount)
                                  .arg(formatSize(result.history.releasedSize)));
        } else {
            setStatusText(QStringLiteral("清理完成：成功 %1 项，释放 %2")
                                  .arg(result.history.successCount)
                                  .arg(formatSize(result.history.releasedSize)));
        }
        setRunning(false);
        refreshHistory();
        if (result.filesystemOperationAttempted)
            emit rescanRequested();
    });
    connect(&m_service, &services::CleanupService::cleanupFailed,
            this, [this](const CleanupRunResult &result) {
        if (!result.plan.items.isEmpty()) {
            m_items.setPlan(result.plan);
            setResultVisible(true);
        }
        setCancelling(false);
        setStatusText(result.errorMessage);
        m_errorMessage = result.errorMessage;
        m_technicalDetail = result.technicalDetail;
        emit errorChanged();
        setRunning(false);
        refreshHistory();
        if (result.filesystemOperationAttempted)
            emit rescanRequested();
    });
    refreshHistory();
}

CleanupPlanModel *CleanupViewModel::items() { return &m_items; }
bool CleanupViewModel::running() const { return m_running; }
bool CleanupViewModel::cancelling() const { return m_cancelling; }
bool CleanupViewModel::resultVisible() const { return m_resultVisible; }
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
    if (m_resultVisible)
        return;
    rebuildPlan();
}

void CleanupViewModel::rebuildPlan()
{
    if (m_running)
        return;
    setResultVisible(false);
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
    if (!m_running || m_cancelling)
        return;
    setCancelling(true);
    setStatusText(QStringLiteral("正在停止清理，将在当前项目结束后停止…"));
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

void CleanupViewModel::setCancelling(bool cancelling)
{
    if (m_cancelling == cancelling)
        return;
    m_cancelling = cancelling;
    emit cancellingChanged();
}

void CleanupViewModel::setResultVisible(bool visible)
{
    if (m_resultVisible == visible)
        return;
    m_resultVisible = visible;
    emit resultVisibleChanged();
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
