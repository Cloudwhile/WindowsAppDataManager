#pragma once

#include "../models/ScanResult.h"

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>

namespace wam::services {

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
    void scanCompleted(const wam::ScanResult &result);
    void scanFailed(const QString &message, const QString &technicalDetail);

private:
    void setTargetPath(QString targetPath);

    QFutureWatcher<ScanResult> m_watcher;
    std::shared_ptr<std::atomic_bool> m_cancelRequested;
    QString m_targetPath;
};

} // namespace wam::services
