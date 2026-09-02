#pragma once

#include "CleanupExecutor.h"
#include "../models/CleanupPlan.h"
#include "../platform/windows/process/RunningProcessCatalog.h"

#include <QFutureWatcher>
#include <QObject>

#include <atomic>
#include <functional>
#include <memory>

namespace wam::services {

class CleanupService final : public QObject {
    Q_OBJECT

public:
    using ProcessQuery = std::function<
            platform::windows::RunningProcessQueryResult()>;

    explicit CleanupService(QObject *parent = nullptr);
    CleanupService(CleanupExecutorPtr executor,
                   QString databasePath,
                   ProcessQuery processQuery,
                   QObject *parent = nullptr);
    ~CleanupService() override;

    [[nodiscard]] bool isRunning() const;
    void execute(CleanupPlan plan, QStringList scanRoots);
    void cancel();

signals:
    void cleanupStarted(const QString &planId);
    void itemChanged(int index,
                     wam::CleanupItemState state,
                     const QString &message,
                     quint64 releasedSize);
    void cleanupCompleted(const wam::CleanupRunResult &result);
    void cleanupFailed(const wam::CleanupRunResult &result);

private:
    QFutureWatcher<CleanupRunResult> m_watcher;
    CleanupExecutorPtr m_executor;
    QString m_databasePath;
    ProcessQuery m_processQuery;
    std::shared_ptr<std::atomic_bool> m_cancelRequested;
};

} // namespace wam::services
