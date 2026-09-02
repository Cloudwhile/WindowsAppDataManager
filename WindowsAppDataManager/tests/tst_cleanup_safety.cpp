#include "src/core/cleanup/CleanupValidator.h"
#include "src/core/scanner/DirectoryScanner.h"
#include "src/platform/windows/filesystem/DeletionAccessProbe.h"
#include "src/platform/windows/filesystem/StablePathIdentity.h"
#include "src/services/CleanupExecutor.h"
#include "src/services/CleanupService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <memory>
#include <optional>

namespace {

constexpr auto builtInRuleSource = "内置规则 / sample-app@1";

wam::platform::windows::RunningProcessQueryResult completeProcessSnapshot()
{
    wam::platform::windows::RunningProcessQueryResult result;
    result.supported = true;
    result.available = true;
    result.enumerationComplete = true;
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
    candidate.impact = QStringLiteral("缓存可以重新生成");
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
            || file.write("temporary cleanup safety payload") <= 0) {
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
    return candidate;
}

class AbortingCleanupExecutor final : public wam::services::CleanupExecutor {
public:
    wam::services::CleanupExecutionOutcome moveToRecycleBin(
            const wam::CleanupCandidateInfo &) override
    {
        m_callCount.fetch_add(1, std::memory_order_relaxed);
        return {
            false, true, true, 0,
            QStringLiteral("用户取消了回收站操作"), {}
        };
    }

    [[nodiscard]] int callCount() const
    {
        return m_callCount.load(std::memory_order_relaxed);
    }

private:
    std::atomic_int m_callCount = 0;
};

class MovingAbortingCleanupExecutor final : public wam::services::CleanupExecutor {
public:
    wam::services::CleanupExecutionOutcome moveToRecycleBin(
            const wam::CleanupCandidateInfo &candidate) override
    {
        const QString recycledPath = candidate.path
                + QStringLiteral(".fake-recycle-aborted");
        const bool moved = !QFileInfo::exists(recycledPath)
                && QDir().rename(candidate.path, recycledPath);
        return {
            false, moved, true, 0,
            QStringLiteral("回收站操作已中止"),
            moved ? QStringLiteral("项目已移动后收到中止状态")
                  : QStringLiteral("测试执行器无法移动项目")
        };
    }
};

} // namespace

class CleanupSafetyTest final : public QObject {
    Q_OBJECT

private slots:
    void validatorHonorsPreCancelledToken();
    void deletionProbeHonorsPreCancelledToken();
    void validatorUsesTargetNameForUnreadableProcesses_data();
    void validatorUsesTargetNameForUnreadableProcesses();
    void serviceStopsAfterAbortedOutcome();
    void serviceCountsMovedItemWhenExecutorReportsAborted();
};

void CleanupSafetyTest::validatorHonorsPreCancelledToken()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto candidate = syntheticCandidate(temporary.path());
    std::atomic_bool cancelRequested = true;

    const auto result = wam::core::CleanupValidator::validate(
            candidate, {temporary.path()}, completeProcessSnapshot(),
            cancelRequested);

    QCOMPARE(result.state, wam::core::CleanupValidationState::Cancelled);
}

void CleanupSafetyTest::deletionProbeHonorsPreCancelledToken()
{
    std::atomic_bool cancelRequested = true;
    const auto result = wam::platform::windows::DeletionAccessProbe::probe(
            QStringLiteral("unused"), cancelRequested);
    QVERIFY(result.cancelled);
    QVERIFY(!result.available);
}

void CleanupSafetyTest::validatorUsesTargetNameForUnreadableProcesses_data()
{
    QTest::addColumn<QString>("imageName");
    QTest::addColumn<bool>("enumerationComplete");
    QTest::addColumn<int>("expectedState");
    QTest::newRow("unrelated") << QStringLiteral("other.exe") << true
            << static_cast<int>(wam::core::CleanupValidationState::Ready);
    QTest::newRow("matching") << QStringLiteral("sample.exe") << true
            << static_cast<int>(wam::core::CleanupValidationState::Blocked);
    QTest::newRow("unnamed") << QString() << true
            << static_cast<int>(wam::core::CleanupValidationState::Blocked);
    QTest::newRow("interrupted") << QStringLiteral("other.exe") << false
            << static_cast<int>(wam::core::CleanupValidationState::Blocked);
}

void CleanupSafetyTest::validatorUsesTargetNameForUnreadableProcesses()
{
    QFETCH(QString, imageName);
    QFETCH(bool, enumerationComplete);
    QFETCH(int, expectedState);

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString scanRoot =
            QDir(temporary.path()).filePath(QStringLiteral("Local"));
    const wam::CleanupCandidateInfo candidate = syntheticCandidate(scanRoot);
    wam::platform::windows::RunningProcessQueryResult processes;
    processes.supported = true;
    processes.available = true;
    processes.enumerationComplete = enumerationComplete;
    processes.complete = false;
    processes.processes.append({1234, imageName, {}});

    const auto result = wam::core::CleanupValidator::validateProcessState(
            candidate, processes);
    QCOMPARE(static_cast<int>(result.state), expectedState);
}

void CleanupSafetyTest::serviceStopsAfterAbortedOutcome()
{
#ifndef Q_OS_WIN
    QSKIP("清理前验证仅在 Windows 上可用");
#else
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    QString error;
    const auto candidate = createFilesystemCandidate(
            root, QStringLiteral("AbortCache"), &error);
    QVERIFY2(candidate.has_value(), qPrintable(error));

    wam::CleanupPlan plan;
    plan.id = QStringLiteral("abort-run");
    plan.createdAt = QDateTime::currentDateTimeUtc();
    plan.items = {{*candidate}, {*candidate}};
    plan.items[0].selected = true;
    plan.items[1].selected = true;
    plan.items[1].candidate.id = QStringLiteral("abort-cache-second");

    const auto executor = std::make_shared<AbortingCleanupExecutor>();
    wam::services::CleanupService service(
            executor,
            QDir(temporary.path()).filePath(QStringLiteral("abort.sqlite3")),
            [] { return completeProcessSnapshot(); });
    QSignalSpy completed(&service, &wam::services::CleanupService::cleanupCompleted);
    service.execute(plan, {root});
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 10000);

    const auto result = qvariant_cast<wam::CleanupRunResult>(
            completed.constFirst().constFirst());
    QCOMPARE(executor->callCount(), 1);
    QVERIFY(result.cancelled);
    QCOMPARE(result.history.successCount, 0);
    QCOMPARE(result.history.failureCount, 0);
    QCOMPARE(result.plan.items[0].state, wam::CleanupItemState::Skipped);
    QCOMPARE(result.plan.items[1].state, wam::CleanupItemState::Skipped);
    QVERIFY(QFileInfo::exists(candidate->path));
#endif
}

void CleanupSafetyTest::serviceCountsMovedItemWhenExecutorReportsAborted()
{
#ifndef Q_OS_WIN
    QSKIP("清理前身份与删除访问验证仅在 Windows 上可用");
#else
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString root = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    QString error;
    const auto candidate = createFilesystemCandidate(
            root, QStringLiteral("MovedThenAbortedCache"), &error);
    QVERIFY2(candidate.has_value(), qPrintable(error));

    wam::CleanupPlan plan;
    plan.id = QStringLiteral("moved-then-aborted-run");
    plan.createdAt = QDateTime::currentDateTimeUtc();
    plan.items = {{*candidate}};
    plan.items[0].selected = true;
    plan.estimatedSize = candidate->size;

    const auto executor = std::make_shared<MovingAbortingCleanupExecutor>();
    wam::services::CleanupService service(
            executor,
            QDir(temporary.path()).filePath(
                    QStringLiteral("moved-then-aborted.sqlite3")),
            [] { return completeProcessSnapshot(); });
    QSignalSpy completed(&service, &wam::services::CleanupService::cleanupCompleted);
    service.execute(plan, {root});
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 10000);

    const auto result = qvariant_cast<wam::CleanupRunResult>(
            completed.constFirst().constFirst());
    QVERIFY(result.cancelled);
    QVERIFY(result.filesystemOperationAttempted);
    QVERIFY(!QFileInfo::exists(candidate->path));
    QCOMPARE(result.history.successCount, 1);
    QCOMPARE(result.history.failureCount, 0);
    QCOMPARE(result.history.releasedSize, candidate->size);
    QCOMPARE(result.plan.items.constFirst().state, wam::CleanupItemState::Done);
    QCOMPARE(result.plan.items.constFirst().releasedSize, candidate->size);
    QVERIFY(result.history.recoverable);
#endif
}

QTEST_GUILESS_MAIN(CleanupSafetyTest)

#include "tst_cleanup_safety.moc"
