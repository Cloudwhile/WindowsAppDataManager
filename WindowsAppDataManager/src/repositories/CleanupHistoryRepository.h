#pragma once

#include "../models/CleanupPlan.h"

#include <QSqlDatabase>

namespace wam::repositories {

class CleanupHistoryRepository final {
public:
    explicit CleanupHistoryRepository(QString databasePath = {});
    ~CleanupHistoryRepository();

    CleanupHistoryRepository(const CleanupHistoryRepository &) = delete;
    CleanupHistoryRepository &operator=(const CleanupHistoryRepository &) = delete;

    [[nodiscard]] static QString defaultDatabasePath();
    [[nodiscard]] QString databasePath() const;

    bool initialize(QString *errorMessage = nullptr);
    bool recordPlan(const CleanupPlan &plan, QString *errorMessage = nullptr);
    bool updateItem(const QString &runId,
                    const CleanupPlanItem &item,
                    const QString &technicalDetail,
                    bool recoverable,
                    QString *errorMessage = nullptr);
    bool completeRun(const CleanupHistoryRecord &record,
                     QString *errorMessage = nullptr);
    [[nodiscard]] QVector<CleanupHistoryRecord> recentRuns(
            int limit = 20,
            QString *errorMessage = nullptr);

private:
    bool open(QString *errorMessage);
    bool executeSchema(QString *errorMessage);

    QString m_databasePath;
    QString m_connectionName;
    QSqlDatabase m_database;
};

} // namespace wam::repositories
