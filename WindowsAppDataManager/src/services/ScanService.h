#pragma once

#include "../models/ScanResult.h"

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>

namespace wam::services {

struct ScanUpdateDispatchState;

class ScanService final : public QObject {
    Q_OBJECT

public:
    explicit ScanService(QObject *parent = nullptr);
    ~ScanService() override;

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] QString targetPath() const;
    void startScan();
    void startScan(const QStringList &roots);
    void cancelScan();

signals:
    void targetPathChanged();
    void scanStarted();
    void progressChanged(int progress, const QString &currentPath);
    void scanUpdatesReady(const QVector<wam::ApplicationInfo> &applications,
                          int issueCount,
                          int completedTargets,
                          int totalTargets);
    void scanCompleted(const wam::ScanResult &result);
    void scanFailed(const QString &message, const QString &technicalDetail);

private:
    void deliverPendingUpdates(
            quint64 generation,
            const std::shared_ptr<ScanUpdateDispatchState> &state);
    void setTargetPath(QString targetPath);

    QFutureWatcher<ScanResult> m_watcher;
    std::shared_ptr<std::atomic_bool> m_cancelRequested;
    QString m_targetPath;
    quint64 m_scanGeneration = 0;
    bool m_acceptingUpdates = false;
};

} // namespace wam::services
