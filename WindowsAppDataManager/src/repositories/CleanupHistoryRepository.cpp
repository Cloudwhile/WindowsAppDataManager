#include "CleanupHistoryRepository.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <limits>

namespace wam::repositories {
namespace {

qint64 databaseInteger(quint64 value)
{
    return static_cast<qint64>(std::min<quint64>(
            value, static_cast<quint64>(std::numeric_limits<qint64>::max())));
}

void setError(QString *target, const QString &message)
{
    if (target)
        *target = message;
}

QString itemStateValue(CleanupItemState state)
{
    switch (state) {
    case CleanupItemState::Pending: return QStringLiteral("pending");
    case CleanupItemState::Validating: return QStringLiteral("validating");
    case CleanupItemState::Ready: return QStringLiteral("ready");
    case CleanupItemState::Cleaning: return QStringLiteral("cleaning");
    case CleanupItemState::Done: return QStringLiteral("done");
    case CleanupItemState::Skipped: return QStringLiteral("skipped");
    case CleanupItemState::Failed: return QStringLiteral("failed");
    }
    return QStringLiteral("failed");
}

int selectedItemCount(const CleanupPlan &plan)
{
    return static_cast<int>(std::count_if(
            plan.items.cbegin(), plan.items.cend(), [](const CleanupPlanItem &item) {
        return item.selected;
    }));
}

} // namespace

CleanupHistoryRepository::CleanupHistoryRepository(QString databasePath)
    : m_databasePath(databasePath.isEmpty()
                     ? defaultDatabasePath() : std::move(databasePath)),
      m_connectionName(QStringLiteral("wam-cleanup-%1").arg(
              QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
}

CleanupHistoryRepository::~CleanupHistoryRepository()
{
    if (m_database.isValid())
        m_database.close();
    m_database = {};
    if (!m_connectionName.isEmpty())
        QSqlDatabase::removeDatabase(m_connectionName);
}

QString CleanupHistoryRepository::defaultDatabasePath()
{
    const QString overridePath = qEnvironmentVariable("WAM_DATABASE_PATH").trimmed();
    if (!overridePath.isEmpty())
        return QDir::cleanPath(overridePath);
    return QDir(QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation))
            .filePath(QStringLiteral("history.sqlite3"));
}

QString CleanupHistoryRepository::databasePath() const
{
    return m_databasePath;
}

bool CleanupHistoryRepository::open(QString *errorMessage)
{
    if (m_database.isOpen())
        return true;

    const QFileInfo databaseFile(m_databasePath);
    if (!QDir().mkpath(databaseFile.absolutePath())) {
        setError(errorMessage, QStringLiteral("无法创建数据库目录：%1")
                                      .arg(databaseFile.absolutePath()));
        return false;
    }

    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                           m_connectionName);
    m_database.setDatabaseName(m_databasePath);
    if (!m_database.open()) {
        setError(errorMessage, QStringLiteral("无法打开清理历史数据库：%1")
                                      .arg(m_database.lastError().text()));
        return false;
    }

    QSqlQuery pragma(m_database);
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        setError(errorMessage, QStringLiteral("无法启用数据库外键：%1")
                                      .arg(pragma.lastError().text()));
        return false;
    }
    return true;
}

bool CleanupHistoryRepository::executeSchema(QString *errorMessage)
{
    static const QStringList statements {
        QStringLiteral(
                "CREATE TABLE IF NOT EXISTS cleanup_runs ("
                "run_id TEXT PRIMARY KEY, created_at TEXT NOT NULL, "
                "completed_at TEXT, estimated_bytes INTEGER NOT NULL, "
                "released_bytes INTEGER NOT NULL DEFAULT 0, "
                "item_count INTEGER NOT NULL, success_count INTEGER NOT NULL DEFAULT 0, "
                "failure_count INTEGER NOT NULL DEFAULT 0, recoverable INTEGER NOT NULL DEFAULT 0)"),
        QStringLiteral(
                "CREATE TABLE IF NOT EXISTS cleanup_items ("
                "run_id TEXT NOT NULL, candidate_id TEXT NOT NULL, "
                "application_id TEXT NOT NULL, application_name TEXT NOT NULL, "
                "path TEXT NOT NULL, rule_entry_id TEXT NOT NULL, "
                "rule_source TEXT NOT NULL, risk INTEGER NOT NULL, "
                "rebuildable INTEGER NOT NULL, selected INTEGER NOT NULL, "
                "state TEXT NOT NULL, estimated_bytes INTEGER NOT NULL, "
                "estimated_files INTEGER NOT NULL, metadata_fingerprint TEXT NOT NULL, "
                "volume_serial TEXT NOT NULL, file_index TEXT NOT NULL, "
                "released_bytes INTEGER NOT NULL DEFAULT 0, "
                "message TEXT, technical_detail TEXT, recoverable INTEGER NOT NULL DEFAULT 0, "
                "PRIMARY KEY (run_id, candidate_id), "
                "FOREIGN KEY (run_id) REFERENCES cleanup_runs(run_id) ON DELETE CASCADE)"),
        QStringLiteral(
                "CREATE INDEX IF NOT EXISTS cleanup_runs_created_at "
                "ON cleanup_runs(created_at DESC)")
    };

    for (const QString &statement : statements) {
        QSqlQuery query(m_database);
        if (!query.exec(statement)) {
            setError(errorMessage, QStringLiteral("无法初始化清理历史数据库：%1")
                                          .arg(query.lastError().text()));
            return false;
        }
    }
    return true;
}

bool CleanupHistoryRepository::initialize(QString *errorMessage)
{
    return open(errorMessage) && executeSchema(errorMessage);
}

bool CleanupHistoryRepository::recordPlan(
        const CleanupPlan &plan, QString *errorMessage)
{
    if (!initialize(errorMessage))
        return false;
    if (!m_database.transaction()) {
        setError(errorMessage, m_database.lastError().text());
        return false;
    }

    QSqlQuery run(m_database);
    run.prepare(QStringLiteral(
            "INSERT INTO cleanup_runs "
            "(run_id, created_at, estimated_bytes, item_count) "
            "VALUES (?, ?, ?, ?)"));
    run.addBindValue(plan.id);
    run.addBindValue(plan.createdAt.toUTC().toString(Qt::ISODateWithMs));
    run.addBindValue(databaseInteger(plan.estimatedSize));
    run.addBindValue(selectedItemCount(plan));
    if (!run.exec()) {
        m_database.rollback();
        setError(errorMessage, run.lastError().text());
        return false;
    }

    QSqlQuery item(m_database);
    item.prepare(QStringLiteral(
            "INSERT INTO cleanup_items "
            "(run_id, candidate_id, application_id, application_name, path, "
            "rule_entry_id, rule_source, risk, rebuildable, selected, state, "
            "estimated_bytes, estimated_files, metadata_fingerprint, "
            "volume_serial, file_index) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    for (const CleanupPlanItem &planItem : plan.items) {
        item.bindValue(0, plan.id);
        item.bindValue(1, planItem.candidate.id);
        item.bindValue(2, planItem.candidate.applicationId);
        item.bindValue(3, planItem.candidate.applicationName);
        item.bindValue(4, planItem.candidate.path);
        item.bindValue(5, planItem.candidate.ruleEntryId);
        item.bindValue(6, planItem.candidate.ruleSource);
        item.bindValue(7, static_cast<int>(planItem.candidate.risk));
        item.bindValue(8, static_cast<int>(planItem.candidate.rebuildable));
        item.bindValue(9, planItem.selected ? 1 : 0);
        item.bindValue(10, itemStateValue(planItem.state));
        item.bindValue(11, databaseInteger(planItem.candidate.size));
        item.bindValue(12, databaseInteger(planItem.candidate.fileCount));
        item.bindValue(13, planItem.candidate.metadataFingerprint);
        item.bindValue(14, QString::number(
                               planItem.candidate.volumeSerialNumber));
        item.bindValue(15, QString::number(planItem.candidate.fileIndex));
        if (!item.exec()) {
            m_database.rollback();
            setError(errorMessage, item.lastError().text());
            return false;
        }
    }

    if (!m_database.commit()) {
        setError(errorMessage, m_database.lastError().text());
        return false;
    }
    return true;
}

bool CleanupHistoryRepository::updateItem(
        const QString &runId,
        const CleanupPlanItem &item,
        const QString &technicalDetail,
        bool recoverable,
        QString *errorMessage)
{
    if (!initialize(errorMessage))
        return false;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
            "UPDATE cleanup_items SET state = ?, released_bytes = ?, message = ?, "
            "technical_detail = ?, recoverable = ? "
            "WHERE run_id = ? AND candidate_id = ?"));
    query.addBindValue(itemStateValue(item.state));
    query.addBindValue(databaseInteger(item.releasedSize));
    query.addBindValue(item.statusMessage);
    query.addBindValue(technicalDetail);
    query.addBindValue(recoverable ? 1 : 0);
    query.addBindValue(runId);
    query.addBindValue(item.candidate.id);
    if (!query.exec()) {
        setError(errorMessage, query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() != 1) {
        setError(errorMessage, QStringLiteral(
                              "清理项目审计记录不存在或未被更新"));
        return false;
    }
    return true;
}

bool CleanupHistoryRepository::completeRun(
        const CleanupHistoryRecord &record,
        QString *errorMessage)
{
    if (!initialize(errorMessage))
        return false;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
            "UPDATE cleanup_runs SET completed_at = ?, released_bytes = ?, "
            "success_count = ?, failure_count = ?, recoverable = ? WHERE run_id = ?"));
    query.addBindValue(record.completedAt.toUTC().toString(Qt::ISODateWithMs));
    query.addBindValue(databaseInteger(record.releasedSize));
    query.addBindValue(record.successCount);
    query.addBindValue(record.failureCount);
    query.addBindValue(record.recoverable ? 1 : 0);
    query.addBindValue(record.runId);
    if (!query.exec()) {
        setError(errorMessage, query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() != 1) {
        setError(errorMessage, QStringLiteral(
                              "清理运行审计记录不存在或未被更新"));
        return false;
    }
    return true;
}

QVector<CleanupHistoryRecord> CleanupHistoryRepository::recentRuns(
        int limit,
        QString *errorMessage)
{
    QVector<CleanupHistoryRecord> records;
    if (!initialize(errorMessage))
        return records;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
            "SELECT run_id, created_at, completed_at, estimated_bytes, released_bytes, "
            "item_count, success_count, failure_count, recoverable "
            "FROM cleanup_runs ORDER BY created_at DESC LIMIT ?"));
    query.addBindValue(std::clamp(limit, 1, 100));
    if (!query.exec()) {
        setError(errorMessage, query.lastError().text());
        return records;
    }
    while (query.next()) {
        CleanupHistoryRecord record;
        record.runId = query.value(0).toString();
        record.createdAt = QDateTime::fromString(query.value(1).toString(),
                                                 Qt::ISODateWithMs);
        record.completedAt = QDateTime::fromString(query.value(2).toString(),
                                                   Qt::ISODateWithMs);
        record.estimatedSize = query.value(3).toULongLong();
        record.releasedSize = query.value(4).toULongLong();
        record.itemCount = query.value(5).toInt();
        record.successCount = query.value(6).toInt();
        record.failureCount = query.value(7).toInt();
        record.recoverable = query.value(8).toBool();
        records.append(std::move(record));
    }
    return records;
}

} // namespace wam::repositories
