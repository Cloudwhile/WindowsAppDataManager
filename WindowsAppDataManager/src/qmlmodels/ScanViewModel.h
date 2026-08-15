#pragma once

#include "ApplicationListModel.h"
#include "../services/ScanService.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace wam::qmlmodels {

class ScanViewModel : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(ScanViewModel)
    QML_UNCREATABLE("由 Backend 提供")
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentPathChanged)
    Q_PROPERTY(QString targetPath READ targetPath NOTIFY targetPathChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString lastScanText READ lastScanText NOTIFY lastScanTextChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)
    Q_PROPERTY(QString technicalDetail READ technicalDetail NOTIFY errorChanged)
    Q_PROPERTY(int issueCount READ issueCount NOTIFY issueCountChanged)
    Q_PROPERTY(bool partialResult READ partialResult NOTIFY issueCountChanged)

public:
    explicit ScanViewModel(ApplicationListModel *applicationModel, QObject *parent = nullptr);

    [[nodiscard]] bool running() const;
    [[nodiscard]] int progress() const;
    [[nodiscard]] QString currentPath() const;
    [[nodiscard]] QString targetPath() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QString lastScanText() const;
    [[nodiscard]] QString errorMessage() const;
    [[nodiscard]] QString technicalDetail() const;
    [[nodiscard]] int issueCount() const;
    [[nodiscard]] bool partialResult() const;

    Q_INVOKABLE void toggleScan();
    Q_INVOKABLE void startScan();
    Q_INVOKABLE void cancelScan();

signals:
    void runningChanged();
    void progressChanged();
    void currentPathChanged();
    void targetPathChanged();
    void statusTextChanged();
    void lastScanTextChanged();
    void errorChanged();
    void issueCountChanged();

private:
    void setRunning(bool running);
    void setProgress(int progress);
    void setCurrentPath(QString path);
    void setStatusText(QString status);
    void clearError();

    ApplicationListModel *m_applicationModel = nullptr;
    services::ScanService m_service;
    QStringList m_roots;
    QString m_targetPath;
    QString m_currentPath;
    QString m_statusText = QStringLiteral("尚未扫描");
    QString m_lastScanText = QStringLiteral("尚未扫描");
    QString m_errorMessage;
    QString m_technicalDetail;
    bool m_running = false;
    int m_progress = 0;
    int m_issueCount = 0;
};

} // namespace wam::qmlmodels
