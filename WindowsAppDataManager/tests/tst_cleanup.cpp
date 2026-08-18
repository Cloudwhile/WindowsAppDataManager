#include "src/core/cleanup/CleanupPlanBuilder.h"
#include "src/core/cleanup/CleanupValidator.h"
#include "src/core/scanner/DirectoryScanner.h"
#include "src/platform/windows/filesystem/StablePathIdentity.h"
#include "src/qmlmodels/CleanupPlanModel.h"
#include "src/repositories/CleanupHistoryRepository.h"
#include "src/services/CleanupExecutor.h"
#include "src/services/CleanupService.h"
#include "src/services/ScanService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>
#include <stdexcept>

namespace {

constexpr auto builtInRuleSource = "内置规则 / sample-app@1";

wam::platform::windows::RunningProcessQueryResult completeProcessSnapshot()
{
    wam::platform::windows::RunningProcessQueryResult result;
    result.supported = true;
    result.available = true;
    result.complete = true;
    return result;
}

wam::CleanupCandidateInfo syntheticCandidate(const QString &scanRoot)
{
    const QString applicationRoot = QDir(scanRoot).filePath(
            QStringLiteral("Sample/App Data"));
    wam::CleanupCandidateInfo candidate;
    candidate.id = QStringLiteral("sample-cache");
    candidate.applicationId = QStringLiteral("sample-app");
    candidate.applicationName = QStringLiteral("Sample App");
    candidate.applicationRoot = applicationRoot;
    candidate.executablePath = QDir(scanRoot).filePath(
            QStringLiteral("Sample/sample.exe"));
    candidate.path = QDir(applicationRoot).filePath(QStringLiteral("Cache"));
    candidate.ruleEntryId = QStringLiteral("cache");
    candidate.ruleSource = QString::fromUtf8(builtInRuleSource);
    candidate.category = wam::DataCategory::Cache;
    candidate.risk = wam::RiskLevel::Safe;
    candidate.rebuildable = wam::RebuildableState::Yes;
    candidate.impact = QStringLiteral("缓存可以重新生成。");
    candidate.size = 4096;
    candidate.fileCount = 2;
    candidate.metadataFingerprint = QStringLiteral("test-fingerprint");
    candidate.volumeSerialNumber = 1;
    candidate.fileIndex = 2;
    candidate.identityValid = true;
    candidate.directory = true;
    candidate.verifiedRule = true;
    candidate.exclusiveLocation = true;
    candidate.scanComplete = true;
    return candidate;
}

wam::ApplicationInfo eligibleApplication(const QString &scanRoot)
{
    wam::ApplicationInfo application;
    application.id = QStringLiteral("sample-app");
    application.name = QStringLiteral("Sample App");
    application.installState = wam::InstallState::Installed;
    application.confidence = 96;
    application.scanComplete = true;
    application.evidence.append({
        wam::EvidenceSource::RunningProcess,
        wam::EvidenceStatus::NotFound,
        QStringLiteral("完整进程快照中未发现匹配进程")
    });
    application.cleanupCandidates.append(syntheticCandidate(scanRoot));
    return application;
}

wam::core::CleanupPlanBuildContext buildContext(const QString &scanRoot)
{
    wam::core::CleanupPlanBuildContext context;
    context.scanRoots = {scanRoot};
    context.createdAt = QDateTime::fromString(
            QStringLiteral("2026-08-17T08:00:00Z"), Qt::ISODate);
    return context;
}

std::optional<wam::CleanupCandidateInfo> createFilesystemCandidate(
        const QString &scanRoot,
        const QString &candidateName,
        QString *errorMessage)
{
    const QString applicationRoot = QDir(scanRoot).filePath(
            QStringLiteral("Sample/App Data"));
    const QString candidatePath = QDir(applicationRoot).filePath(candidateName);
    if (!QDir().mkpath(candidatePath)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法创建测试候选目录");
        return std::nullopt;
    }

    QFile file(QDir(candidatePath).filePath(QStringLiteral("payload.bin")));
    if (!file.open(QIODevice::WriteOnly)
            || file.write("temporary cleanup test payload") <= 0) {
        if (errorMessage)
            *errorMessage = file.errorString();
        return std::nullopt;
    }
    file.close();

    const auto identity =
            wam::platform::windows::StablePathIdentityReader::read(candidatePath);
    if (identity.state != wam::platform::windows::StablePathState::Present
            || !identity.identity.valid || !identity.identity.directory) {
        if (errorMessage)
            *errorMessage = identity.technicalDetail;
        return std::nullopt;
    }

    std::atomic_bool cancelRequested = false;
    const wam::core::DirectoryScanStats stats = wam::core::DirectoryScanner().scan(
            candidatePath, cancelRequested, {}, {}, {}, true);
    if (stats.cancelled || !stats.issues.isEmpty() || !stats.stabilityVerified
            || stats.metadataFingerprint.isEmpty()) {
        if (errorMessage) {
            *errorMessage = stats.issues.isEmpty()
                    ? QStringLiteral("无法形成稳定测试快照")
                    : stats.issues.constFirst().technicalDetail;
        }
        return std::nullopt;
    }

    wam::CleanupCandidateInfo candidate = syntheticCandidate(scanRoot);
    candidate.id = candidateName.toCaseFolded();
    candidate.applicationRoot = applicationRoot;
    candidate.path = candidatePath;
    candidate.size = stats.totalSize;
    candidate.fileCount = stats.fileCount;
    candidate.metadataFingerprint = stats.metadataFingerprint;
    candidate.volumeSerialNumber = identity.identity.volumeSerialNumber;
    candidate.fileIndex = identity.identity.fileIndex;
    candidate.identityValid = true;
    candidate.directory = true;
    return candidate;
}

class FakeCleanupExecutor final : public wam::services::CleanupExecutor {
public:
    wam::services::CleanupExecutionOutcome moveToRecycleBin(
            const wam::CleanupCandidateInfo &candidate) override
    {
        const QString &path = candidate.path;
        {
            QMutexLocker locker(&m_mutex);
            m_paths.append(QDir::cleanPath(path));
        }

        const QString recycledPath = path + QStringLiteral(".fake-recycle");
        const bool moved = !QFileInfo::exists(recycledPath)
                && QDir().rename(path, recycledPath);
        return {
            moved,
            moved,
            false,
            0,
            moved ? QStringLiteral("假执行器已移动测试目录")
                  : QStringLiteral("假执行器无法移动测试目录"),
            {}
        };
    }

    [[nodiscard]] QStringList paths() const
    {
        QMutexLocker locker(&m_mutex);
        return m_paths;
    }

private:
    mutable QMutex m_mutex;
    QStringList m_paths;
};

class ThrowingCleanupExecutor final : public wam::services::CleanupExecutor {
public:
    wam::services::CleanupExecutionOutcome moveToRecycleBin(
            const wam::CleanupCandidateInfo &) override
    {
        m_callCount.fetch_add(1, std::memory_order_relaxed);
        throw std::runtime_error("测试执行器异常");
    }

    [[nodiscard]] int callCount() const
    {
        return m_callCount.load(std::memory_order_relaxed);
    }

private:
    std::atomic_int m_callCount = 0;
};

} // namespace

class CleanupTest final : public QObject {
    Q_OBJECT

private slots:
    void planBuilderIncludesOnlyEligibleCandidatesByDefaultUnselected();
    void planBuilderExcludesUnsafeCandidates_data();
    void planBuilderExcludesUnsafeCandidates();
    void planBuilderRejectsOverlappingPaths();
    void planModelRequiresExplicitPendingSelection();
    void scanBuildsExactCandidateAndDetectsSensitivePollution();
    void validatorBlocksRunningApplication();
    void validatorBlocksIdentityChange();
    void validatorBlocksFingerprintChange();
    void historyRepositoryRecordsPlanItemsAndSummary();
    void historyRepositoryRejectsMissingUpdates();
    void serviceUsesFakeExecutorForSelectedPendingItems();
    void serviceRechecksProcessesBeforeExecutor();
    void serviceCompletesAuditAfterExecutorException();
    void serviceRejectsSelectedTerminalItem();
};

void CleanupTest::planBuilderIncludesOnlyEligibleCandidatesByDefaultUnselected()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString scanRoot = QDir(temporary.path()).filePath(QStringLiteral("Local"));

    const wam::CleanupPlan plan = wam::core::CleanupPlanBuilder::build(
            {eligibleApplication(scanRoot)}, buildContext(scanRoot));

    QVERIFY(!plan.id.isEmpty());
    QCOMPARE(plan.createdAt, buildContext(scanRoot).createdAt);
    QCOMPARE(plan.items.size(), 1);
    QCOMPARE(plan.excludedCount, 0);
    QCOMPARE(plan.estimatedSize, quint64(4096));
    QVERIFY(plan.exclusionReasons.isEmpty());
    QVERIFY(!plan.items.constFirst().selected);
    QCOMPARE(plan.items.constFirst().state, wam::CleanupItemState::Pending);
}

void CleanupTest::planBuilderExcludesUnsafeCandidates_data()
{
    QTest::addColumn<int>("scenario");
    QTest::newRow("application-not-installed") << 0;
    QTest::newRow("low-attribution-confidence") << 1;
    QTest::newRow("application-scan-incomplete") << 2;
    QTest::newRow("candidate-scan-incomplete") << 3;
    QTest::newRow("rule-not-verified") << 4;
    QTest::newRow("rule-not-built-in") << 5;
    QTest::newRow("shared-location") << 6;
    QTest::newRow("risk-not-safe") << 7;
    QTest::newRow("not-rebuildable") << 8;
    QTest::newRow("contains-unsafe-data") << 9;
    QTest::newRow("missing-files") << 10;
    QTest::newRow("missing-fingerprint") << 11;
    QTest::newRow("invalid-identity") << 12;
    QTest::newRow("not-directory") << 13;
    QTest::newRow("outside-application-root") << 14;
    QTest::newRow("outside-scan-root") << 15;
    QTest::newRow("missing-process-evidence") << 16;
    QTest::newRow("process-evidence-incomplete") << 17;
    QTest::newRow("candidate-application-mismatch") << 18;
    QTest::newRow("missing-executable-path") << 19;
    QTest::newRow("missing-candidate-id") << 20;
    QTest::newRow("missing-rule-entry-id") << 21;
}

void CleanupTest::planBuilderExcludesUnsafeCandidates()
{
    QFETCH(int, scenario);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString scanRoot = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    wam::ApplicationInfo application = eligibleApplication(scanRoot);
    wam::CleanupCandidateInfo &candidate = application.cleanupCandidates.first();
    wam::core::CleanupPlanBuildContext context = buildContext(scanRoot);

    switch (scenario) {
    case 0:
        application.installState = wam::InstallState::Unknown;
        break;
    case 1:
        application.confidence = context.minimumApplicationConfidence - 1;
        break;
    case 2:
        application.scanComplete = false;
        break;
    case 3:
        candidate.scanComplete = false;
        break;
    case 4:
        candidate.verifiedRule = false;
        break;
    case 5:
        candidate.ruleSource = QStringLiteral("本地规则 / sample-app@1");
        break;
    case 6:
        candidate.exclusiveLocation = false;
        break;
    case 7:
        candidate.risk = wam::RiskLevel::Low;
        break;
    case 8:
        candidate.rebuildable = wam::RebuildableState::No;
        break;
    case 9:
        candidate.containsUnsafeData = true;
        break;
    case 10:
        candidate.fileCount = 0;
        break;
    case 11:
        candidate.metadataFingerprint.clear();
        break;
    case 12:
        candidate.identityValid = false;
        break;
    case 13:
        candidate.directory = false;
        break;
    case 14:
        candidate.path = QDir(scanRoot).filePath(QStringLiteral("Other/Cache"));
        break;
    case 15:
        context.scanRoots = {
            QDir(temporary.path()).filePath(QStringLiteral("DifferentRoot"))
        };
        break;
    case 16:
        application.evidence.clear();
        break;
    case 17:
        application.evidence.first().status = wam::EvidenceStatus::Incomplete;
        break;
    case 18:
        candidate.applicationId = QStringLiteral("different-app");
        break;
    case 19:
        candidate.executablePath.clear();
        break;
    case 20:
        candidate.id.clear();
        break;
    case 21:
        candidate.ruleEntryId.clear();
        break;
    default:
        QFAIL("未知清理计划排除场景");
    }

    const wam::CleanupPlan plan = wam::core::CleanupPlanBuilder::build(
            {application}, context);

    QVERIFY(plan.items.isEmpty());
    QCOMPARE(plan.estimatedSize, quint64(0));
    QCOMPARE(plan.excludedCount, 1);
    QCOMPARE(plan.exclusionReasons.size(), 1);
}

void CleanupTest::planBuilderRejectsOverlappingPaths()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString scanRoot = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    wam::ApplicationInfo application = eligibleApplication(scanRoot);
    wam::CleanupCandidateInfo nested = application.cleanupCandidates.constFirst();
    nested.id = QStringLiteral("nested-cache");
    nested.path = QDir(nested.path).filePath(QStringLiteral("Code Cache"));
    application.cleanupCandidates.append(nested);

    const wam::CleanupPlan plan = wam::core::CleanupPlanBuilder::build(
            {application}, buildContext(scanRoot));

    QCOMPARE(plan.items.size(), 1);
    QCOMPARE(plan.excludedCount, 1);
    QVERIFY(plan.exclusionReasons.constFirst().contains(QStringLiteral("重叠")));
}

void CleanupTest::planModelRequiresExplicitPendingSelection()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString scanRoot = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    const wam::CleanupPlan plan = wam::core::CleanupPlanBuilder::build(
            {eligibleApplication(scanRoot)}, buildContext(scanRoot));
    QCOMPARE(plan.items.size(), 1);

    wam::qmlmodels::CleanupPlanModel model;
    QSignalSpy summarySpy(&model, &wam::qmlmodels::CleanupPlanModel::summaryChanged);
    model.setPlan(plan);
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.selectedCount(), 0);
    QCOMPARE(model.data(model.index(0),
                        wam::qmlmodels::CleanupPlanModel::SelectedRole).toBool(),
             false);

    model.setSelected(0, true);
    QCOMPARE(model.selectedCount(), 1);
    QCOMPARE(model.data(model.index(0),
                        wam::qmlmodels::CleanupPlanModel::SelectedRole).toBool(),
             true);
    model.updateItem(0, wam::CleanupItemState::Validating,
                     QStringLiteral("正在验证"), 0);
    model.setSelected(0, false);
    QCOMPARE(model.selectedCount(), 1);
    QVERIFY(summarySpy.count() >= 3);
}

void CleanupTest::scanBuildsExactCandidateAndDetectsSensitivePollution()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString localRoot = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    const QString roamingRoot = QDir(temporary.path()).filePath(QStringLiteral("Roaming"));
    const QString cachePath = QDir(roamingRoot).filePath(
            QStringLiteral("discord/Cache"));
    QVERIFY(QDir().mkpath(localRoot));
    QVERIFY(QDir().mkpath(cachePath));

    QFile cacheFile(QDir(cachePath).filePath(QStringLiteral("cache.bin")));
    QVERIFY(cacheFile.open(QIODevice::WriteOnly));
    QCOMPARE(cacheFile.write(QByteArray(16, 'c')), qint64(16));
    cacheFile.close();

    wam::services::ScanService service;
    QSignalSpy completedSpy(&service, &wam::services::ScanService::scanCompleted);
    QSignalSpy failedSpy(&service, &wam::services::ScanService::scanFailed);
    QVERIFY(completedSpy.isValid());
    QVERIFY(failedSpy.isValid());
    service.startScan({localRoot, roamingRoot});
    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 10000);
    QCOMPARE(failedSpy.count(), 0);

    const auto firstResult = qvariant_cast<wam::ScanResult>(
            completedSpy.constFirst().constFirst());
    const auto firstApplication = std::find_if(
            firstResult.applications.cbegin(), firstResult.applications.cend(),
            [](const wam::ApplicationInfo &application) {
        return application.id == QStringLiteral("discord");
    });
    QVERIFY(firstApplication != firstResult.applications.cend());
    const auto firstCandidate = std::find_if(
            firstApplication->cleanupCandidates.cbegin(),
            firstApplication->cleanupCandidates.cend(),
            [&cachePath](const wam::CleanupCandidateInfo &candidate) {
        return QDir::cleanPath(candidate.path).compare(
                QDir::cleanPath(cachePath), Qt::CaseInsensitive) == 0;
    });
    QVERIFY(firstCandidate != firstApplication->cleanupCandidates.cend());
    QVERIFY(firstCandidate->verifiedRule);
    QVERIFY(firstCandidate->exclusiveLocation);
    QVERIFY(firstCandidate->scanComplete);
    QVERIFY(firstCandidate->identityValid);
    QVERIFY(firstCandidate->directory);
    QVERIFY(!firstCandidate->containsUnsafeData);
    QCOMPARE(firstCandidate->fileCount, quint64(1));
    QCOMPARE(firstCandidate->size, quint64(16));
    QVERIFY(!firstCandidate->metadataFingerprint.isEmpty());

    QFile sensitiveFile(QDir(cachePath).filePath(QStringLiteral("Login Data.sqlite")));
    QVERIFY(sensitiveFile.open(QIODevice::WriteOnly));
    QCOMPARE(sensitiveFile.write(QByteArray(8, 's')), qint64(8));
    sensitiveFile.close();

    service.startScan({localRoot, roamingRoot});
    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 2, 10000);
    QCOMPARE(failedSpy.count(), 0);
    const auto secondResult = qvariant_cast<wam::ScanResult>(
            completedSpy.at(1).constFirst());
    const auto secondApplication = std::find_if(
            secondResult.applications.cbegin(), secondResult.applications.cend(),
            [](const wam::ApplicationInfo &application) {
        return application.id == QStringLiteral("discord");
    });
    QVERIFY(secondApplication != secondResult.applications.cend());
    const auto pollutedCandidate = std::find_if(
            secondApplication->cleanupCandidates.cbegin(),
            secondApplication->cleanupCandidates.cend(),
            [&cachePath](const wam::CleanupCandidateInfo &candidate) {
        return QDir::cleanPath(candidate.path).compare(
                QDir::cleanPath(cachePath), Qt::CaseInsensitive) == 0;
    });
    QVERIFY(pollutedCandidate != secondApplication->cleanupCandidates.cend());
    QVERIFY(pollutedCandidate->containsUnsafeData);
}

void CleanupTest::validatorBlocksRunningApplication()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString scanRoot = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    wam::CleanupCandidateInfo candidate = syntheticCandidate(scanRoot);
    wam::platform::windows::RunningProcessQueryResult processes =
            completeProcessSnapshot();
    processes.processes.append({
        1234,
        QStringLiteral("sample.exe"),
        candidate.executablePath
    });

    const auto result = wam::core::CleanupValidator::validate(
            candidate, {scanRoot}, processes);

    QCOMPARE(result.state, wam::core::CleanupValidationState::Blocked);
    QVERIFY(result.message.contains(QStringLiteral("仍在运行")));
}

void CleanupTest::validatorBlocksIdentityChange()
{
#ifndef Q_OS_WIN
    QSKIP("稳定路径身份仅在 Windows 上可用");
#else
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString scanRoot = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    QString error;
    const auto candidateResult = createFilesystemCandidate(
            scanRoot, QStringLiteral("Cache"), &error);
    QVERIFY2(candidateResult.has_value(), qPrintable(error));
    wam::CleanupCandidateInfo candidate = *candidateResult;
    ++candidate.fileIndex;

    const auto result = wam::core::CleanupValidator::validate(
            candidate, {scanRoot}, completeProcessSnapshot());

    QCOMPARE(result.state, wam::core::CleanupValidationState::Blocked);
    QVERIFY(result.message.contains(QStringLiteral("身份")));
#endif
}

void CleanupTest::validatorBlocksFingerprintChange()
{
#ifndef Q_OS_WIN
    QSKIP("稳定路径身份与删除访问探测仅在 Windows 上可用");
#else
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString scanRoot = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    QString error;
    const auto candidateResult = createFilesystemCandidate(
            scanRoot, QStringLiteral("Cache"), &error);
    QVERIFY2(candidateResult.has_value(), qPrintable(error));
    const wam::CleanupCandidateInfo candidate = *candidateResult;

    QFile file(QDir(candidate.path).filePath(QStringLiteral("payload.bin")));
    QVERIFY(file.open(QIODevice::Append));
    QVERIFY(file.write("changed after scan") > 0);
    file.close();

    const auto result = wam::core::CleanupValidator::validate(
            candidate, {scanRoot}, completeProcessSnapshot());

    QCOMPARE(result.state, wam::core::CleanupValidationState::Blocked);
    QVERIFY(result.message.contains(QStringLiteral("内容")));
#endif
}

void CleanupTest::historyRepositoryRecordsPlanItemsAndSummary()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QVERIFY(QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")));
    const QString databasePath = QDir(temporary.path()).filePath(
            QStringLiteral("cleanup-history.sqlite3"));

    wam::CleanupPlan plan;
    plan.id = QStringLiteral("history-run");
    plan.createdAt = QDateTime::fromString(
            QStringLiteral("2026-08-17T08:00:00Z"), Qt::ISODate);
    plan.items = {
        {syntheticCandidate(temporary.path())},
        {syntheticCandidate(temporary.path())}
    };
    plan.items[0].candidate.id = QStringLiteral("done-item");
    plan.items[1].candidate.id = QStringLiteral("failed-item");
    plan.items[0].selected = true;
    plan.items[1].selected = true;
    plan.estimatedSize = plan.items[0].candidate.size
            + plan.items[1].candidate.size;

    QString error;
    {
        wam::repositories::CleanupHistoryRepository repository(databasePath);
        QVERIFY2(repository.recordPlan(plan, &error), qPrintable(error));

        plan.items[0].state = wam::CleanupItemState::Done;
        plan.items[0].releasedSize = plan.items[0].candidate.size;
        plan.items[0].statusMessage = QStringLiteral("测试完成");
        QVERIFY2(repository.updateItem(plan.id, plan.items[0],
                                       QStringLiteral("fake executor"), true,
                                       &error), qPrintable(error));

        plan.items[1].state = wam::CleanupItemState::Failed;
        plan.items[1].statusMessage = QStringLiteral("测试失败");
        QVERIFY2(repository.updateItem(plan.id, plan.items[1],
                                       QStringLiteral("validation blocked"), false,
                                       &error), qPrintable(error));

        wam::CleanupHistoryRecord completed;
        completed.runId = plan.id;
        completed.completedAt = plan.createdAt.addSecs(5);
        completed.releasedSize = plan.items[0].releasedSize;
        completed.successCount = 1;
        completed.failureCount = 1;
        completed.recoverable = true;
        QVERIFY2(repository.completeRun(completed, &error), qPrintable(error));

        const QVector<wam::CleanupHistoryRecord> recent =
                repository.recentRuns(1, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(recent.size(), 1);
        QCOMPARE(recent.constFirst().runId, plan.id);
        QCOMPARE(recent.constFirst().estimatedSize, plan.estimatedSize);
        QCOMPARE(recent.constFirst().releasedSize, plan.items[0].releasedSize);
        QCOMPARE(recent.constFirst().itemCount, 2);
        QCOMPARE(recent.constFirst().successCount, 1);
        QCOMPARE(recent.constFirst().failureCount, 1);
        QVERIFY(recent.constFirst().recoverable);
    }

    const QString connectionName = QStringLiteral("cleanup-test-inspection");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
                QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        QVERIFY2(database.open(), qPrintable(database.lastError().text()));
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral(
                "SELECT candidate_id, state, released_bytes, recoverable, "
                "rule_entry_id, risk, rebuildable, selected, estimated_files, "
                "metadata_fingerprint, volume_serial, file_index "
                "FROM cleanup_items ORDER BY candidate_id")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("done-item"));
        QCOMPARE(query.value(1).toString(), QStringLiteral("done"));
        QCOMPARE(query.value(2).toULongLong(), plan.items[0].releasedSize);
        QVERIFY(query.value(3).toBool());
        QCOMPARE(query.value(4).toString(), QStringLiteral("cache"));
        QCOMPARE(query.value(5).toInt(), static_cast<int>(wam::RiskLevel::Safe));
        QCOMPARE(query.value(6).toInt(),
                 static_cast<int>(wam::RebuildableState::Yes));
        QVERIFY(query.value(7).toBool());
        QCOMPARE(query.value(8).toULongLong(), quint64(2));
        QCOMPARE(query.value(9).toString(), QStringLiteral("test-fingerprint"));
        QCOMPARE(query.value(10).toString(), QStringLiteral("1"));
        QCOMPARE(query.value(11).toString(), QStringLiteral("2"));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("failed-item"));
        QCOMPARE(query.value(1).toString(), QStringLiteral("failed"));
        QCOMPARE(query.value(2).toULongLong(), quint64(0));
        QVERIFY(!query.value(3).toBool());
        QVERIFY(!query.next());
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
}

void CleanupTest::historyRepositoryRejectsMissingUpdates()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QVERIFY(QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")));
    const QString databasePath = QDir(temporary.path()).filePath(
            QStringLiteral("missing-history.sqlite3"));

    wam::CleanupPlan plan;
    plan.id = QStringLiteral("existing-run");
    plan.createdAt = QDateTime::currentDateTimeUtc();
    plan.items = {{syntheticCandidate(temporary.path())}};
    plan.items[0].selected = true;
    plan.estimatedSize = plan.items[0].candidate.size;

    wam::repositories::CleanupHistoryRepository repository(databasePath);
    QString error;
    QVERIFY2(repository.recordPlan(plan, &error), qPrintable(error));

    wam::CleanupPlanItem missingItem = plan.items.constFirst();
    missingItem.candidate.id = QStringLiteral("missing-item");
    missingItem.state = wam::CleanupItemState::Failed;
    QVERIFY(!repository.updateItem(plan.id, missingItem, {}, false, &error));
    QVERIFY(!error.isEmpty());

    error.clear();
    wam::CleanupHistoryRecord missingRun;
    missingRun.runId = QStringLiteral("missing-run");
    missingRun.completedAt = QDateTime::currentDateTimeUtc();
    QVERIFY(!repository.completeRun(missingRun, &error));
    QVERIFY(!error.isEmpty());
}

void CleanupTest::serviceUsesFakeExecutorForSelectedPendingItems()
{
#ifndef Q_OS_WIN
    QSKIP("清理前身份与删除访问验证仅在 Windows 上可用");
#else
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QVERIFY(QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")));
    const QString scanRoot = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    const QString databasePath = QDir(temporary.path()).filePath(
            QStringLiteral("service-history.sqlite3"));

    QString error;
    const auto selectedCandidate = createFilesystemCandidate(
            scanRoot, QStringLiteral("SelectedCache"), &error);
    QVERIFY2(selectedCandidate.has_value(), qPrintable(error));
    const auto unselectedCandidate = createFilesystemCandidate(
            scanRoot, QStringLiteral("UnselectedCache"), &error);
    QVERIFY2(unselectedCandidate.has_value(), qPrintable(error));
    wam::CleanupPlan plan;
    plan.id = QStringLiteral("service-run");
    plan.createdAt = QDateTime::currentDateTimeUtc();
    plan.items = {
        {*selectedCandidate},
        {*unselectedCandidate}
    };
    plan.items[0].selected = true;
    plan.items[0].state = wam::CleanupItemState::Pending;
    plan.items[1].selected = false;
    plan.items[1].state = wam::CleanupItemState::Pending;
    plan.estimatedSize = selectedCandidate->size
            + unselectedCandidate->size;

    const auto executor = std::make_shared<FakeCleanupExecutor>();
    wam::services::CleanupService service(
            executor,
            databasePath,
            [] { return completeProcessSnapshot(); });
    QSignalSpy completedSpy(
            &service, &wam::services::CleanupService::cleanupCompleted);
    QSignalSpy failedSpy(
            &service, &wam::services::CleanupService::cleanupFailed);
    QVERIFY(completedSpy.isValid());
    QVERIFY(failedSpy.isValid());

    service.execute(plan, {scanRoot});
    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 10000);
    QCOMPARE(failedSpy.count(), 0);

    const wam::CleanupRunResult result = qvariant_cast<wam::CleanupRunResult>(
            completedSpy.constFirst().constFirst());
    QVERIFY(result.errorMessage.isEmpty());
    QVERIFY(!result.cancelled);
    QVERIFY(result.filesystemOperationAttempted);
    QCOMPARE(executor->paths(), QStringList {QDir::cleanPath(selectedCandidate->path)});
    QVERIFY(!QFileInfo::exists(selectedCandidate->path));
    QVERIFY(QFileInfo::exists(unselectedCandidate->path));
    QCOMPARE(result.history.estimatedSize, selectedCandidate->size);
    QCOMPARE(result.history.itemCount, 1);
    QCOMPARE(result.history.successCount, 1);
    QCOMPARE(result.history.failureCount, 0);
    QCOMPARE(result.history.releasedSize, selectedCandidate->size);
    QVERIFY(result.history.recoverable);

    wam::repositories::CleanupHistoryRepository repository(databasePath);
    const QVector<wam::CleanupHistoryRecord> recent = repository.recentRuns(1, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(recent.size(), 1);
    QCOMPARE(recent.constFirst().estimatedSize, selectedCandidate->size);
    QCOMPARE(recent.constFirst().itemCount, 1);
    QCOMPARE(recent.constFirst().successCount, 1);
#endif
}

void CleanupTest::serviceRechecksProcessesBeforeExecutor()
{
#ifndef Q_OS_WIN
    QSKIP("清理前身份与删除访问验证仅在 Windows 上可用");
#else
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QVERIFY(QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")));
    const QString scanRoot = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    const QString databasePath = QDir(temporary.path()).filePath(
            QStringLiteral("process-recheck.sqlite3"));

    QString error;
    const auto candidate = createFilesystemCandidate(
            scanRoot, QStringLiteral("ProcessRecheckCache"), &error);
    QVERIFY2(candidate.has_value(), qPrintable(error));

    wam::CleanupPlan plan;
    plan.id = QStringLiteral("process-recheck-run");
    plan.createdAt = QDateTime::currentDateTimeUtc();
    plan.items = {{*candidate}};
    plan.items[0].selected = true;
    plan.estimatedSize = candidate->size;

    const auto executor = std::make_shared<FakeCleanupExecutor>();
    const auto queryCount = std::make_shared<std::atomic_int>(0);
    wam::services::CleanupService service(
            executor,
            databasePath,
            [queryCount, executablePath = candidate->executablePath] {
        auto processes = completeProcessSnapshot();
        if (queryCount->fetch_add(1, std::memory_order_relaxed) == 1) {
            processes.processes.append({
                1234,
                QStringLiteral("sample.exe"),
                executablePath
            });
        }
        return processes;
    });
    QSignalSpy completedSpy(
            &service, &wam::services::CleanupService::cleanupCompleted);
    QSignalSpy failedSpy(
            &service, &wam::services::CleanupService::cleanupFailed);

    service.execute(plan, {scanRoot});
    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 10000);
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(queryCount->load(std::memory_order_relaxed), 2);
    QVERIFY(executor->paths().isEmpty());
    QVERIFY(QFileInfo::exists(candidate->path));

    const wam::CleanupRunResult result = qvariant_cast<wam::CleanupRunResult>(
            completedSpy.constFirst().constFirst());
    QVERIFY(!result.filesystemOperationAttempted);
    QCOMPARE(result.history.failureCount, 1);
    QCOMPARE(result.plan.items.constFirst().state,
             wam::CleanupItemState::Failed);
    QVERIFY(result.plan.items.constFirst().statusMessage.contains(
            QStringLiteral("仍在运行")));
#endif
}

void CleanupTest::serviceCompletesAuditAfterExecutorException()
{
#ifndef Q_OS_WIN
    QSKIP("清理前身份与删除访问验证仅在 Windows 上可用");
#else
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QVERIFY(QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")));
    const QString scanRoot = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    const QString databasePath = QDir(temporary.path()).filePath(
            QStringLiteral("exception-history.sqlite3"));

    QString error;
    const auto candidate = createFilesystemCandidate(
            scanRoot, QStringLiteral("ExceptionCache"), &error);
    QVERIFY2(candidate.has_value(), qPrintable(error));

    wam::CleanupPlan plan;
    plan.id = QStringLiteral("exception-run");
    plan.createdAt = QDateTime::currentDateTimeUtc();
    plan.items = {{*candidate}};
    plan.items[0].selected = true;
    plan.estimatedSize = candidate->size;

    const auto executor = std::make_shared<ThrowingCleanupExecutor>();
    wam::services::CleanupService service(
            executor,
            databasePath,
            [] { return completeProcessSnapshot(); });
    QSignalSpy completedSpy(
            &service, &wam::services::CleanupService::cleanupCompleted);
    QSignalSpy failedSpy(
            &service, &wam::services::CleanupService::cleanupFailed);

    service.execute(plan, {scanRoot});
    QTRY_COMPARE_WITH_TIMEOUT(failedSpy.count(), 1, 10000);
    QCOMPARE(completedSpy.count(), 0);
    QCOMPARE(executor->callCount(), 1);
    QVERIFY(QFileInfo::exists(candidate->path));
    QCOMPARE(failedSpy.constFirst().at(0).toString(),
             QStringLiteral("清理任务未能完成"));
    QVERIFY(failedSpy.constFirst().at(1).toString().contains(
            QStringLiteral("测试执行器异常")));

    {
        wam::repositories::CleanupHistoryRepository repository(databasePath);
        const QVector<wam::CleanupHistoryRecord> recent =
                repository.recentRuns(1, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(recent.size(), 1);
        QVERIFY(recent.constFirst().completedAt.isValid());
        QCOMPARE(recent.constFirst().successCount, 0);
        QCOMPARE(recent.constFirst().failureCount, 1);
    }

    const QString connectionName = QStringLiteral("cleanup-exception-inspection");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(
                QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(databasePath);
        QVERIFY2(database.open(), qPrintable(database.lastError().text()));
        QSqlQuery query(database);
        query.prepare(QStringLiteral(
                "SELECT state, technical_detail FROM cleanup_items "
                "WHERE run_id = ? AND candidate_id = ?"));
        query.addBindValue(plan.id);
        query.addBindValue(candidate->id);
        QVERIFY2(query.exec(), qPrintable(query.lastError().text()));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("failed"));
        QVERIFY(query.value(1).toString().contains(
                QStringLiteral("测试执行器异常")));
        QVERIFY(!query.next());
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
#endif
}

void CleanupTest::serviceRejectsSelectedTerminalItem()
{
#ifndef Q_OS_WIN
    QSKIP("稳定路径身份仅在 Windows 上可用");
#else
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString scanRoot = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    QString error;
    const auto candidate = createFilesystemCandidate(
            scanRoot, QStringLiteral("CompletedCache"), &error);
    QVERIFY2(candidate.has_value(), qPrintable(error));

    wam::CleanupPlan plan;
    plan.id = QStringLiteral("terminal-run");
    plan.createdAt = QDateTime::currentDateTimeUtc();
    plan.items = {{*candidate}};
    plan.items[0].selected = true;
    plan.items[0].state = wam::CleanupItemState::Done;
    plan.estimatedSize = candidate->size;

    const auto executor = std::make_shared<FakeCleanupExecutor>();
    const QString databasePath = QDir(temporary.path()).filePath(
            QStringLiteral("terminal-history.sqlite3"));
    wam::services::CleanupService service(
            executor,
            databasePath,
            [] { return completeProcessSnapshot(); });
    QSignalSpy completedSpy(
            &service, &wam::services::CleanupService::cleanupCompleted);
    QSignalSpy failedSpy(
            &service, &wam::services::CleanupService::cleanupFailed);

    service.execute(plan, {scanRoot});

    QCOMPARE(completedSpy.count(), 0);
    QCOMPARE(failedSpy.count(), 1);
    QVERIFY(executor->paths().isEmpty());
    QVERIFY(QFileInfo::exists(candidate->path));
    QVERIFY(!QFileInfo::exists(databasePath));
#endif
}

QTEST_GUILESS_MAIN(CleanupTest)

#include "tst_cleanup.moc"
