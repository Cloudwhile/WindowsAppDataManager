#include "CleanupService.h"

#include "../core/cleanup/CleanupValidator.h"
#include "../platform/windows/filesystem/StablePathIdentity.h"
#include "../platform/windows/process/RunningProcessCatalog.h"
#include "../platform/windows/shell/RecycleBinExecutor.h"
#include "../repositories/CleanupHistoryRepository.h"

#include <QMetaObject>
#include <QtConcurrentRun>

#include <exception>
#include <utility>

namespace wam::services {
namespace {

using ItemCallback = std::function<void(
        int, CleanupItemState, const QString &, quint64)>;

bool isTerminalState(CleanupItemState state)
{
    return state == CleanupItemState::Done
            || state == CleanupItemState::Skipped
            || state == CleanupItemState::Failed;
}

void appendTechnicalDetail(QString &target, const QString &detail)
{
    if (detail.isEmpty())
        return;
    if (!target.isEmpty())
        target.append(QLatin1Char('\n'));
    target.append(detail);
}

bool matchesExpectedIdentity(
        const CleanupCandidateInfo &candidate,
        const platform::windows::StablePathIdentityResult &identity)
{
    return candidate.identityValid && candidate.directory
            && identity.state == platform::windows::StablePathState::Present
            && identity.identity.valid && identity.identity.directory
            && identity.identity.volumeSerialNumber == candidate.volumeSerialNumber
            && identity.identity.fileIndex == candidate.fileIndex;
}

void publishItem(const ItemCallback &callback,
                 int index,
                 CleanupPlanItem &item,
                 CleanupItemState state,
                 QString message)
{
    item.state = state;
    item.statusMessage = std::move(message);
    callback(index, item.state, item.statusMessage, item.releasedSize);
}

CleanupRunResult performCleanup(
        CleanupPlan plan,
        const QStringList &scanRoots,
        const CleanupExecutorPtr &executor,
        const CleanupService::ProcessQuery &processQuery,
        const QString &databasePath,
        const std::shared_ptr<std::atomic_bool> &cancelRequested,
        const ItemCallback &itemCallback)
{
    CleanupRunResult result;
    result.plan = std::move(plan);
    result.history.runId = result.plan.id;
    result.history.createdAt = result.plan.createdAt;
    result.history.estimatedSize = result.plan.estimatedSize;
    for (const CleanupPlanItem &item : std::as_const(result.plan.items)) {
        if (item.selected)
            ++result.history.itemCount;
    }

    repositories::CleanupHistoryRepository history(databasePath);
    QString databaseError;
    try {
        if (!history.recordPlan(result.plan, &databaseError)) {
            result.errorMessage = QStringLiteral("无法创建清理审计记录");
            result.technicalDetail = databaseError;
            return result;
        }
    } catch (const std::exception &error) {
        result.errorMessage = QStringLiteral("无法创建清理审计记录");
        result.technicalDetail = QString::fromUtf8(error.what());
        return result;
    } catch (...) {
        result.errorMessage = QStringLiteral("无法创建清理审计记录");
        result.technicalDetail = QStringLiteral("未知数据库异常");
        return result;
    }

    bool allSuccessfulItemsRecoverable = true;
    bool auditAborted = false;
    int activeIndex = -1;
    const auto abortForAudit = [&](const QString &message) {
        if (result.errorMessage.isEmpty()) {
            result.errorMessage = QStringLiteral("无法更新清理审计记录");
            result.technicalDetail = message;
        }
        auditAborted = true;
    };
    const auto updateItem = [&](const CleanupPlanItem &item,
                                const QString &technicalDetail,
                                bool recoverable) {
        QString updateError;
        if (history.updateItem(result.plan.id, item, technicalDetail,
                               recoverable, &updateError)) {
            return true;
        }
        abortForAudit(updateError);
        return false;
    };

    try {
        for (int index = 0; index < result.plan.items.size(); ++index) {
            activeIndex = index;
            CleanupPlanItem &item = result.plan.items[index];
            if (!item.selected) {
                publishItem(itemCallback, index, item, CleanupItemState::Skipped,
                            QStringLiteral("未选择该项"));
                if (!updateItem(item, {}, false))
                    break;
                continue;
            }
            if (cancelRequested->load(std::memory_order_relaxed)) {
                result.cancelled = true;
                publishItem(itemCallback, index, item, CleanupItemState::Skipped,
                            QStringLiteral("清理已取消"));
                if (!updateItem(item, {}, false))
                    break;
                continue;
            }

            publishItem(itemCallback, index, item, CleanupItemState::Validating,
                        QStringLiteral("正在执行清理前安全检查"));
            const platform::windows::RunningProcessQueryResult processes =
                    processQuery();
            const core::CleanupValidationResult validation =
                    core::CleanupValidator::validate(
                            item.candidate, scanRoots, processes,
                            *cancelRequested);
            if (validation.state == core::CleanupValidationState::Cancelled) {
                result.cancelled = true;
                publishItem(itemCallback, index, item, CleanupItemState::Skipped,
                            validation.message);
                if (!updateItem(item, validation.technicalDetail, false))
                    break;
                continue;
            }
            if (validation.state == core::CleanupValidationState::Missing) {
                publishItem(itemCallback, index, item, CleanupItemState::Skipped,
                            validation.message);
                if (!updateItem(item, validation.technicalDetail, false))
                    break;
                continue;
            }
            if (validation.state != core::CleanupValidationState::Ready) {
                ++result.history.failureCount;
                publishItem(itemCallback, index, item, CleanupItemState::Failed,
                            validation.message);
                if (!updateItem(item, validation.technicalDetail, false))
                    break;
                continue;
            }

            const core::CleanupValidationResult processRecheck =
                    core::CleanupValidator::validateProcessState(
                            item.candidate, processQuery());
            if (processRecheck.state != core::CleanupValidationState::Ready) {
                ++result.history.failureCount;
                publishItem(itemCallback, index, item, CleanupItemState::Failed,
                            processRecheck.message);
                if (!updateItem(item, processRecheck.technicalDetail, false))
                    break;
                continue;
            }
            if (cancelRequested->load(std::memory_order_relaxed)) {
                result.cancelled = true;
                publishItem(itemCallback, index, item, CleanupItemState::Skipped,
                            QStringLiteral("清理已取消"));
                if (!updateItem(item, {}, false))
                    break;
                continue;
            }

            publishItem(itemCallback, index, item, CleanupItemState::Ready,
                        validation.message);
            publishItem(itemCallback, index, item, CleanupItemState::Cleaning,
                        QStringLiteral("正在移动到回收站"));
            if (!updateItem(item, {}, false))
                break;

            result.filesystemOperationAttempted = true;
            const CleanupExecutionOutcome outcome = executor->moveToRecycleBin(
                    item.candidate);
            if (outcome.aborted) {
                result.cancelled = true;
                cancelRequested->store(true, std::memory_order_relaxed);
                const auto presence =
                        platform::windows::StablePathIdentityReader::read(
                                item.candidate.path);
                QString technicalDetail = outcome.technicalDetail;
                appendTechnicalDetail(technicalDetail, presence.technicalDetail);

                if (presence.state == platform::windows::StablePathState::Missing) {
                    item.releasedSize = item.candidate.size;
                    result.history.releasedSize += item.releasedSize;
                    ++result.history.successCount;
                    allSuccessfulItemsRecoverable = allSuccessfulItemsRecoverable
                            && outcome.recoverable;
                    publishItem(itemCallback, index, item, CleanupItemState::Done,
                                QStringLiteral("已移动到回收站，后续操作已中止"));
                } else if (matchesExpectedIdentity(item.candidate, presence)) {
                    publishItem(itemCallback, index, item,
                                CleanupItemState::Skipped,
                                outcome.message.isEmpty()
                                        ? QStringLiteral("回收站操作已中止")
                                        : outcome.message);
                } else {
                    ++result.history.failureCount;
                    publishItem(
                            itemCallback, index, item, CleanupItemState::Failed,
                            presence.state
                                            == platform::windows::StablePathState::Unavailable
                                    ? QStringLiteral("回收站操作中止后无法确认目标状态")
                                    : QStringLiteral("回收站操作中止后目标身份已变化"));
                }
                if (!updateItem(item, technicalDetail,
                                item.state == CleanupItemState::Done
                                        && outcome.recoverable))
                    break;
                continue;
            }
            if (!outcome.succeeded) {
                ++result.history.failureCount;
                publishItem(itemCallback, index, item, CleanupItemState::Failed,
                            outcome.message.isEmpty()
                                    ? QStringLiteral("移动到回收站失败")
                                    : outcome.message);
                if (!updateItem(item, outcome.technicalDetail,
                                outcome.recoverable))
                    break;
                continue;
            }

            const auto presence = platform::windows::StablePathIdentityReader::read(
                    item.candidate.path);
            if (presence.state != platform::windows::StablePathState::Missing) {
                ++result.history.failureCount;
                publishItem(itemCallback, index, item, CleanupItemState::Failed,
                            QStringLiteral("回收站操作完成后目标仍然存在"));
                if (!updateItem(item, presence.technicalDetail,
                                outcome.recoverable))
                    break;
                continue;
            }

            item.releasedSize = item.candidate.size;
            result.history.releasedSize += item.releasedSize;
            ++result.history.successCount;
            allSuccessfulItemsRecoverable = allSuccessfulItemsRecoverable
                    && outcome.recoverable;
            publishItem(itemCallback, index, item, CleanupItemState::Done,
                        outcome.message.isEmpty()
                                ? QStringLiteral("已移动到回收站")
                                : outcome.message);
            if (!updateItem(item, outcome.technicalDetail,
                            outcome.recoverable))
                break;
        }
    } catch (const std::exception &error) {
        result.errorMessage = QStringLiteral("清理任务未能完成");
        result.technicalDetail = QString::fromUtf8(error.what());
        auditAborted = true;
    } catch (...) {
        result.errorMessage = QStringLiteral("清理任务未能完成");
        result.technicalDetail = QStringLiteral("清理任务发生未知后台异常");
        auditAborted = true;
    }

    if (auditAborted) {
        try {
            if (activeIndex >= 0 && activeIndex < result.plan.items.size()) {
                CleanupPlanItem &activeItem = result.plan.items[activeIndex];
                if (!isTerminalState(activeItem.state)) {
                    if (activeItem.selected) {
                        ++result.history.failureCount;
                        publishItem(
                                itemCallback, activeIndex, activeItem,
                                CleanupItemState::Failed,
                                QStringLiteral("清理任务异常中止"));
                    } else {
                        publishItem(
                                itemCallback, activeIndex, activeItem,
                                CleanupItemState::Skipped,
                                QStringLiteral("未选择该项"));
                    }
                }
                QString settleError;
                if (!history.updateItem(result.plan.id, activeItem,
                                        result.technicalDetail, false,
                                        &settleError)) {
                    appendTechnicalDetail(
                            result.technicalDetail,
                            QStringLiteral("无法收敛当前项目审计状态：%1")
                                    .arg(settleError));
                }
            }

            for (int index = activeIndex + 1;
                 index < result.plan.items.size(); ++index) {
                CleanupPlanItem &item = result.plan.items[index];
                publishItem(
                        itemCallback, index, item, CleanupItemState::Skipped,
                        item.selected
                                ? QStringLiteral("清理因任务异常中止")
                                : QStringLiteral("未选择该项"));
                QString settleError;
                if (!history.updateItem(result.plan.id, item, {}, false,
                                        &settleError)) {
                    appendTechnicalDetail(
                            result.technicalDetail,
                            QStringLiteral("无法收敛项目 %1 的审计状态：%2")
                                    .arg(item.candidate.id, settleError));
                }
            }
        } catch (const std::exception &error) {
            appendTechnicalDetail(result.technicalDetail,
                                  QString::fromUtf8(error.what()));
        } catch (...) {
            appendTechnicalDetail(result.technicalDetail,
                                  QStringLiteral("收敛审计状态时发生未知异常"));
        }
    }

    result.history.completedAt = QDateTime::currentDateTimeUtc();
    result.history.recoverable = result.history.successCount > 0
            && allSuccessfulItemsRecoverable;
    QString completeError;
    try {
        if (!history.completeRun(result.history, &completeError)) {
            if (result.errorMessage.isEmpty())
                result.errorMessage = QStringLiteral("无法完成清理审计记录");
            appendTechnicalDetail(result.technicalDetail, completeError);
        }
    } catch (const std::exception &error) {
        if (result.errorMessage.isEmpty())
            result.errorMessage = QStringLiteral("无法完成清理审计记录");
        appendTechnicalDetail(result.technicalDetail,
                              QString::fromUtf8(error.what()));
    } catch (...) {
        if (result.errorMessage.isEmpty())
            result.errorMessage = QStringLiteral("无法完成清理审计记录");
        appendTechnicalDetail(result.technicalDetail,
                              QStringLiteral("未知数据库异常"));
    }
    return result;
}

} // namespace

CleanupService::CleanupService(QObject *parent)
    : CleanupService(std::make_shared<platform::windows::RecycleBinExecutor>(),
                     repositories::CleanupHistoryRepository::defaultDatabasePath(),
                     [] { return platform::windows::RunningProcessCatalog::query(); },
                     parent)
{
}

CleanupService::CleanupService(CleanupExecutorPtr executor,
                               QString databasePath,
                               ProcessQuery processQuery,
                               QObject *parent)
    : QObject(parent),
      m_executor(executor ? std::move(executor)
                          : std::make_shared<platform::windows::RecycleBinExecutor>()),
      m_databasePath(std::move(databasePath)),
      m_processQuery(processQuery
                     ? std::move(processQuery)
                     : [] { return platform::windows::RunningProcessCatalog::query(); })
{
    connect(&m_watcher, &QFutureWatcher<CleanupRunResult>::finished,
            this, [this] {
        try {
            const CleanupRunResult result = m_watcher.result();
            if (!result.errorMessage.isEmpty()) {
                emit cleanupFailed(result);
                return;
            }
            emit cleanupCompleted(result);
        } catch (const std::exception &error) {
            CleanupRunResult result;
            result.errorMessage = QStringLiteral("清理任务未能完成");
            result.technicalDetail = QString::fromUtf8(error.what());
            emit cleanupFailed(result);
        } catch (...) {
            CleanupRunResult result;
            result.errorMessage = QStringLiteral("清理任务未能完成");
            result.technicalDetail = QStringLiteral("未知后台任务异常");
            emit cleanupFailed(result);
        }
    });
}

CleanupService::~CleanupService()
{
    cancel();
    m_watcher.waitForFinished();
}

bool CleanupService::isRunning() const
{
    return m_watcher.isRunning();
}

void CleanupService::execute(CleanupPlan plan, QStringList scanRoots)
{
    if (isRunning())
        return;
    const auto rejectPlan = [this, &plan](
            QString message, QString technicalDetail = {}) {
        CleanupRunResult result;
        result.plan = plan;
        result.errorMessage = std::move(message);
        result.technicalDetail = std::move(technicalDetail);
        emit cleanupFailed(result);
    };
    if (plan.id.isEmpty() || plan.items.isEmpty()) {
        rejectPlan(QStringLiteral("没有可执行的清理项目"));
        return;
    }

    quint64 selectedSize = 0;
    bool hasSelectedItem = false;
    for (const CleanupPlanItem &item : std::as_const(plan.items)) {
        if (!item.selected)
            continue;
        if (item.state != CleanupItemState::Pending) {
            rejectPlan(
                    QStringLiteral("清理计划状态已失效"),
                    QStringLiteral("已选择项目不是等待确认状态，请重新生成计划"));
            return;
        }
        hasSelectedItem = true;
        selectedSize += item.candidate.size;
    }
    if (!hasSelectedItem) {
        rejectPlan(QStringLiteral("尚未选择清理项目"));
        return;
    }
    plan.estimatedSize = selectedSize;

    m_cancelRequested = std::make_shared<std::atomic_bool>(false);
    const auto itemCallback = [this](int index,
                                     CleanupItemState state,
                                     const QString &message,
                                     quint64 releasedSize) {
        QMetaObject::invokeMethod(this, [this, index, state, message, releasedSize] {
            emit itemChanged(index, state, message, releasedSize);
        }, Qt::QueuedConnection);
    };

    emit cleanupStarted(plan.id);
    m_watcher.setFuture(QtConcurrent::run(
            [plan = std::move(plan), scanRoots = std::move(scanRoots),
             executor = m_executor, databasePath = m_databasePath,
             processQuery = m_processQuery,
             cancelRequested = m_cancelRequested, itemCallback]() mutable {
        return performCleanup(std::move(plan), scanRoots, executor, processQuery,
                              databasePath, cancelRequested, itemCallback);
    }));
}

void CleanupService::cancel()
{
    if (m_cancelRequested)
        m_cancelRequested->store(true, std::memory_order_relaxed);
}

} // namespace wam::services
