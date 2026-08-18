#pragma once

#include "CleanupPlanModel.h"
#include "../models/ScanResult.h"
#include "../services/CleanupService.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace wam::qmlmodels {

class CleanupViewModel : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(CleanupViewModel)
    QML_UNCREATABLE("由 Backend 提供")
    Q_PROPERTY(wam::qmlmodels::CleanupPlanModel *items READ items CONSTANT)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool hasScan READ hasScan NOTIFY planChanged)
    Q_PROPERTY(bool hasPlan READ hasPlan NOTIFY planChanged)
    Q_PROPERTY(bool canExecute READ canExecute NOTIFY planChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)
    Q_PROPERTY(QString technicalDetail READ technicalDetail NOTIFY errorChanged)
    Q_PROPERTY(QString lastCleanupText READ lastCleanupText NOTIFY historyChanged)
    Q_PROPERTY(QString lastReleasedSizeText READ lastReleasedSizeText NOTIFY historyChanged)

public:
    explicit CleanupViewModel(QObject *parent = nullptr);

    [[nodiscard]] CleanupPlanModel *items();
    [[nodiscard]] bool running() const;
    [[nodiscard]] bool hasScan() const;
    [[nodiscard]] bool hasPlan() const;
    [[nodiscard]] bool canExecute() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QString errorMessage() const;
    [[nodiscard]] QString technicalDetail() const;
    [[nodiscard]] QString lastCleanupText() const;
    [[nodiscard]] QString lastReleasedSizeText() const;

    void setScanResult(const ScanResult &result);

    Q_INVOKABLE void rebuildPlan();
    Q_INVOKABLE void executeSelected();
    Q_INVOKABLE void cancel();

signals:
    void runningChanged();
    void planChanged();
    void statusChanged();
    void errorChanged();
    void historyChanged();
    void rescanRequested();

private:
    void setRunning(bool running);
    void setStatusText(QString status);
    void clearError();
    void refreshHistory();

    CleanupPlanModel m_items;
    services::CleanupService m_service;
    QVector<ApplicationInfo> m_applications;
    QStringList m_scanRoots;
    QString m_statusText = QStringLiteral("完成扫描后可生成安全清理计划");
    QString m_errorMessage;
    QString m_technicalDetail;
    QString m_lastCleanupText = QStringLiteral("尚无清理记录");
    QString m_lastReleasedSizeText = QStringLiteral("0 B");
    bool m_running = false;
    bool m_hasScan = false;
};

} // namespace wam::qmlmodels
