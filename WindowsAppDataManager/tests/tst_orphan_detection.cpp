#include "src/core/resolver/AppResolver.h"
#include "src/core/resolver/OrphanDetector.h"
#include "src/core/rules/RuleCatalog.h"
#include "src/core/rules/RuleLoader.h"
#include "src/core/scanner/DirectoryScanner.h"
#include "src/platform/windows/filesystem/PathPresenceReader.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <atomic>
#include <optional>

namespace {

QDateTime assessedAt()
{
    return QDateTime::fromString(
            QStringLiteral("2026-08-17T08:00:00Z"), Qt::ISODate);
}

wam::core::OrphanDetectionContext detectionContext()
{
    wam::core::OrphanDetectionContext context;
    context.exclusiveLocations = true;
    context.scanCompleted = true;
    context.assessedAt = assessedAt();
    return context;
}

wam::EvidenceInfo evidence(wam::EvidenceSource source,
                           wam::EvidenceStatus status)
{
    return {source, status, QStringLiteral("测试证据")};
}

wam::ApplicationInfo orphanCandidate(bool includeOptionalSources = true)
{
    wam::ApplicationInfo application;
    application.id = QStringLiteral("sample-app");
    application.name = QStringLiteral("Sample App");
    application.installState = wam::InstallState::Unknown;
    application.confidence = 88;
    application.scanComplete = true;
    application.lastModified = assessedAt().addDays(-120);
    application.risk = wam::RiskLevel::High;
    application.reclaimableSize = 4096;
    application.evidence = {
        evidence(wam::EvidenceSource::Rule, wam::EvidenceStatus::Matched),
        evidence(wam::EvidenceSource::Folder, wam::EvidenceStatus::Matched),
        evidence(wam::EvidenceSource::Executable, wam::EvidenceStatus::NotFound),
        evidence(wam::EvidenceSource::InstallPath, wam::EvidenceStatus::NotFound),
        evidence(wam::EvidenceSource::RunningProcess, wam::EvidenceStatus::NotFound)
    };
    if (includeOptionalSources) {
        application.evidence.append(
                evidence(wam::EvidenceSource::Registry, wam::EvidenceStatus::NotFound));
        application.evidence.append(
                evidence(wam::EvidenceSource::Appx, wam::EvidenceStatus::NotFound));
    }
    return application;
}

void setEvidenceStatus(wam::ApplicationInfo &application,
                       wam::EvidenceSource source,
                       wam::EvidenceStatus status)
{
    const auto iterator = std::find_if(
            application.evidence.begin(), application.evidence.end(),
            [source](const wam::EvidenceInfo &item) {
        return item.source == source;
    });
    if (iterator == application.evidence.end()) {
        application.evidence.append(evidence(source, status));
    } else {
        iterator->status = status;
    }
}

void removeEvidence(wam::ApplicationInfo &application,
                    wam::EvidenceSource source)
{
    application.evidence.erase(
            std::remove_if(application.evidence.begin(), application.evidence.end(),
                           [source](const wam::EvidenceInfo &item) {
        return item.source == source;
    }),
            application.evidence.end());
}

QJsonObject ruleObject(const std::optional<QString> &ownership = std::nullopt)
{
    QJsonObject location {
        {QStringLiteral("scope"), QStringLiteral("local")},
        {QStringLiteral("path"), QStringLiteral("Sample/App Data")}
    };
    if (ownership)
        location.insert(QStringLiteral("ownership"), *ownership);

    return {
        {QStringLiteral("id"), QStringLiteral("sample-app")},
        {QStringLiteral("version"), QStringLiteral("1")},
        {QStringLiteral("name"), QStringLiteral("Sample App")},
        {QStringLiteral("publisher"), QStringLiteral("Sample Publisher")},
        {QStringLiteral("applicationCategory"), QStringLiteral("工具")},
        {QStringLiteral("executablePath"),
         QStringLiteral("C:/WAM_TEST_MISSING/sample.exe")},
        {QStringLiteral("installPath"),
         QStringLiteral("C:/WAM_TEST_MISSING/sample")},
        {QStringLiteral("locations"), QJsonArray {location}},
        {QStringLiteral("entries"), QJsonArray {
             QJsonObject {
                 {QStringLiteral("id"), QStringLiteral("cache")},
                 {QStringLiteral("path"), QStringLiteral("Cache")},
                 {QStringLiteral("category"), QStringLiteral("cache")},
                 {QStringLiteral("risk"), QStringLiteral("safe")},
                 {QStringLiteral("rebuildable"), true},
                 {QStringLiteral("impact"), QStringLiteral("缓存可以重新生成。")}
             }
         }}
    };
}

QString issueText(const QVector<wam::RuleLoadIssue> &issues)
{
    QStringList values;
    for (const wam::RuleLoadIssue &issue : issues) {
        values.append(QStringLiteral("%1: %2").arg(issue.field, issue.message));
    }
    return values.join(QStringLiteral(" | "));
}

wam::core::rules::RuleCatalog catalogFor(const QJsonObject &rule)
{
    return wam::core::rules::RuleCatalog::fromJsonDocuments({
        {QStringLiteral("orphan-test.json"),
         QJsonDocument(rule).toJson(QJsonDocument::Compact)}
    });
}

const wam::core::ScanTarget *sampleTarget(
        const QVector<wam::core::ScanTarget> &targets)
{
    const auto iterator = std::find_if(
            targets.cbegin(), targets.cend(), [](const wam::core::ScanTarget &target) {
        return target.application.id == QStringLiteral("sample-app");
    });
    return iterator == targets.cend() ? nullptr : &*iterator;
}

bool hasEvidence(const wam::ApplicationInfo &application,
                 wam::EvidenceSource source,
                 wam::EvidenceStatus status)
{
    return std::any_of(
            application.evidence.cbegin(), application.evidence.cend(),
            [source, status](const wam::EvidenceInfo &item) {
        return item.source == source && item.status == status;
    });
}

void writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
}

} // namespace

class OrphanDetectionTest final : public QObject {
    Q_OBJECT

private slots:
    void completeNegativeEvidenceProducesPotentialOrphan();
    void installedApplicationRemainsInstalled();
    void matchedRequiredEvidenceBlocks_data();
    void matchedRequiredEvidenceBlocks();
    void optionalSourceUncertaintyBlocks_data();
    void optionalSourceUncertaintyBlocks();
    void eligibilityGuardBlocks_data();
    void eligibilityGuardBlocks();
    void incompleteAttributionEvidenceBlocks();
    void missingRequiredEvidenceBlocks_data();
    void missingRequiredEvidenceBlocks();
    void optionalRegistryAndAppxMayBeUnconfigured();
    void potentialOrphanDoesNotChangeRiskOrReclaimableSize();
    void ownershipDefaultsToShared();
    void ownershipParsesExplicitValues_data();
    void ownershipParsesExplicitValues();
    void ownershipRejectsInvalidValue();
    void directoryScannerVerifiesStableSnapshot();
    void pathPresenceReaderDistinguishesDirectoryAndMissingPath();
    void invalidWindowsPathIsUnavailable();
    void appResolverMarksUnavailableDeclaredLocationIncomplete();
    void appResolverMarksMissingScopeIncomplete();
    void appResolverPublishesInstallPathEvidence_data();
    void appResolverPublishesInstallPathEvidence();
};

void OrphanDetectionTest::completeNegativeEvidenceProducesPotentialOrphan()
{
    const wam::ApplicationInfo application = orphanCandidate();
    const wam::OrphanAssessment result = wam::core::OrphanDetector::assess(
            application, detectionContext());

    QVERIFY(result.evaluated);
    QCOMPARE(result.state, wam::InstallState::PotentialOrphan);
    QVERIFY(result.confidence >= 80);
    QVERIFY(result.confidence <= 95);
    QVERIFY(result.blockingReasons.isEmpty());
    QVERIFY(result.supportingEvidence.size() >= 6);
    QCOMPARE(result.assessedAt, assessedAt());
}

void OrphanDetectionTest::installedApplicationRemainsInstalled()
{
    wam::ApplicationInfo application;
    application.installState = wam::InstallState::Installed;

    const wam::OrphanAssessment result = wam::core::OrphanDetector::assess(
            application, {});

    QVERIFY(result.evaluated);
    QCOMPARE(result.state, wam::InstallState::Installed);
    QVERIFY(result.blockingReasons.isEmpty());
}

void OrphanDetectionTest::matchedRequiredEvidenceBlocks_data()
{
    QTest::addColumn<int>("sourceValue");
    QTest::newRow("running-process")
            << static_cast<int>(wam::EvidenceSource::RunningProcess);
    QTest::newRow("install-path")
            << static_cast<int>(wam::EvidenceSource::InstallPath);
}

void OrphanDetectionTest::matchedRequiredEvidenceBlocks()
{
    QFETCH(int, sourceValue);
    const auto source = static_cast<wam::EvidenceSource>(sourceValue);
    wam::ApplicationInfo application = orphanCandidate();
    setEvidenceStatus(application, source, wam::EvidenceStatus::Matched);

    const wam::OrphanAssessment result = wam::core::OrphanDetector::assess(
            application, detectionContext());

    QCOMPARE(result.state, wam::InstallState::Unknown);
    QVERIFY(!result.blockingReasons.isEmpty());
}

void OrphanDetectionTest::optionalSourceUncertaintyBlocks_data()
{
    QTest::addColumn<int>("sourceValue");
    QTest::addColumn<int>("statusValue");
    for (const auto source : {wam::EvidenceSource::Registry,
                              wam::EvidenceSource::Appx}) {
        const QByteArray prefix = source == wam::EvidenceSource::Registry
                ? QByteArrayLiteral("registry") : QByteArrayLiteral("appx");
        for (const auto status : {wam::EvidenceStatus::Incomplete,
                                  wam::EvidenceStatus::Conflict,
                                  wam::EvidenceStatus::Ambiguous}) {
            const QByteArray suffix = status == wam::EvidenceStatus::Incomplete
                    ? QByteArrayLiteral("incomplete")
                    : status == wam::EvidenceStatus::Conflict
                            ? QByteArrayLiteral("conflict")
                            : QByteArrayLiteral("ambiguous");
            QTest::newRow((prefix + '-' + suffix).constData())
                    << static_cast<int>(source) << static_cast<int>(status);
        }
    }
}

void OrphanDetectionTest::optionalSourceUncertaintyBlocks()
{
    QFETCH(int, sourceValue);
    QFETCH(int, statusValue);
    wam::ApplicationInfo application = orphanCandidate();
    setEvidenceStatus(application,
                      static_cast<wam::EvidenceSource>(sourceValue),
                      static_cast<wam::EvidenceStatus>(statusValue));

    const wam::OrphanAssessment result = wam::core::OrphanDetector::assess(
            application, detectionContext());

    QCOMPARE(result.state, wam::InstallState::Unknown);
    QVERIFY(!result.blockingReasons.isEmpty());
}

void OrphanDetectionTest::eligibilityGuardBlocks_data()
{
    QTest::addColumn<int>("guard");
    QTest::newRow("shared-location") << 0;
    QTest::newRow("recent-activity") << 1;
    QTest::newRow("cancelled-scan") << 2;
    QTest::newRow("incomplete-target-scan") << 3;
    QTest::newRow("low-attribution-confidence") << 4;
}

void OrphanDetectionTest::eligibilityGuardBlocks()
{
    QFETCH(int, guard);
    wam::ApplicationInfo application = orphanCandidate();
    wam::core::OrphanDetectionContext context = detectionContext();
    switch (guard) {
    case 0:
        context.exclusiveLocations = false;
        break;
    case 1:
        application.lastModified = assessedAt().addDays(-2);
        break;
    case 2:
        context.scanCompleted = false;
        break;
    case 3:
        application.scanComplete = false;
        break;
    case 4:
        application.confidence = context.minimumAttributionConfidence - 1;
        break;
    default:
        QFAIL("未知资格检查测试场景");
    }

    const wam::OrphanAssessment result = wam::core::OrphanDetector::assess(
            application, context);

    QCOMPARE(result.state, wam::InstallState::Unknown);
    QVERIFY(!result.blockingReasons.isEmpty());
}

void OrphanDetectionTest::incompleteAttributionEvidenceBlocks()
{
    wam::ApplicationInfo application = orphanCandidate();
    application.evidence.append(evidence(
            wam::EvidenceSource::Folder, wam::EvidenceStatus::Unavailable));

    const wam::OrphanAssessment result = wam::core::OrphanDetector::assess(
            application, detectionContext());

    QCOMPARE(result.state, wam::InstallState::Unknown);
    QVERIFY(result.blockingReasons.contains(
            QStringLiteral("缺少精确规则与目录归属证据")));
}

void OrphanDetectionTest::missingRequiredEvidenceBlocks_data()
{
    QTest::addColumn<int>("sourceValue");
    QTest::newRow("executable")
            << static_cast<int>(wam::EvidenceSource::Executable);
    QTest::newRow("install-path")
            << static_cast<int>(wam::EvidenceSource::InstallPath);
    QTest::newRow("running-process")
            << static_cast<int>(wam::EvidenceSource::RunningProcess);
}

void OrphanDetectionTest::missingRequiredEvidenceBlocks()
{
    QFETCH(int, sourceValue);
    wam::ApplicationInfo application = orphanCandidate();
    removeEvidence(application, static_cast<wam::EvidenceSource>(sourceValue));

    const wam::OrphanAssessment result = wam::core::OrphanDetector::assess(
            application, detectionContext());

    QCOMPARE(result.state, wam::InstallState::Unknown);
    QVERIFY(!result.blockingReasons.isEmpty());
}

void OrphanDetectionTest::optionalRegistryAndAppxMayBeUnconfigured()
{
    const wam::ApplicationInfo application = orphanCandidate(false);
    const wam::OrphanAssessment result = wam::core::OrphanDetector::assess(
            application, detectionContext());

    QCOMPARE(result.state, wam::InstallState::PotentialOrphan);
    QVERIFY(result.blockingReasons.isEmpty());
}

void OrphanDetectionTest::potentialOrphanDoesNotChangeRiskOrReclaimableSize()
{
    wam::ApplicationInfo application = orphanCandidate();
    application.risk = wam::RiskLevel::Protected;
    application.reclaimableSize = 123456;
    const wam::RiskLevel originalRisk = application.risk;
    const quint64 originalReclaimableSize = application.reclaimableSize;

    const wam::OrphanAssessment result = wam::core::OrphanDetector::assess(
            application, detectionContext());

    QCOMPARE(result.state, wam::InstallState::PotentialOrphan);
    QCOMPARE(application.risk, originalRisk);
    QCOMPARE(application.reclaimableSize, originalReclaimableSize);
}

void OrphanDetectionTest::ownershipDefaultsToShared()
{
    const wam::core::rules::RuleLoadResult result =
            wam::core::rules::RuleLoader::load(
                    QJsonDocument(ruleObject()).toJson(QJsonDocument::Compact),
                    QStringLiteral("default-ownership.json"));

    QVERIFY2(result.isValid(), qPrintable(issueText(result.issues)));
    QCOMPARE(result.rule->locations.constFirst().ownership,
             wam::RuleLocationOwnership::Shared);
}

void OrphanDetectionTest::ownershipParsesExplicitValues_data()
{
    QTest::addColumn<QString>("ownership");
    QTest::addColumn<int>("expectedValue");
    QTest::newRow("shared") << QStringLiteral("shared")
                             << static_cast<int>(wam::RuleLocationOwnership::Shared);
    QTest::newRow("exclusive") << QStringLiteral("exclusive")
                                << static_cast<int>(wam::RuleLocationOwnership::Exclusive);
}

void OrphanDetectionTest::ownershipParsesExplicitValues()
{
    QFETCH(QString, ownership);
    QFETCH(int, expectedValue);
    const wam::core::rules::RuleLoadResult result =
            wam::core::rules::RuleLoader::load(
                    QJsonDocument(ruleObject(ownership)).toJson(QJsonDocument::Compact),
                    QStringLiteral("explicit-ownership.json"));

    QVERIFY2(result.isValid(), qPrintable(issueText(result.issues)));
    QCOMPARE(static_cast<int>(result.rule->locations.constFirst().ownership),
             expectedValue);
}

void OrphanDetectionTest::ownershipRejectsInvalidValue()
{
    const wam::core::rules::RuleLoadResult result =
            wam::core::rules::RuleLoader::load(
                    QJsonDocument(ruleObject(QStringLiteral("private")))
                            .toJson(QJsonDocument::Compact),
                    QStringLiteral("invalid-ownership.json"));

    QVERIFY(!result.isValid());
    QVERIFY(std::any_of(result.issues.cbegin(), result.issues.cend(),
                        [](const wam::RuleLoadIssue &issue) {
        return issue.code == wam::RuleIssueCode::InvalidValue
                && issue.field == QStringLiteral("locations[0].ownership");
    }));
}

void OrphanDetectionTest::directoryScannerVerifiesStableSnapshot()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString cache = QDir(temporary.path()).filePath(QStringLiteral("Cache"));
    QVERIFY(QDir().mkpath(cache));
    writeFile(QDir(cache).filePath(QStringLiteral("entry.bin")), QByteArray(32, 'x'));

    std::atomic_bool cancelled = false;
    const wam::core::DirectoryScanStats stats =
            wam::core::DirectoryScanner().scan(
                    temporary.path(), cancelled, {}, {}, {}, true);

    QVERIFY(!stats.cancelled);
    QVERIFY2(stats.issues.isEmpty(),
             qPrintable(stats.issues.isEmpty()
                                ? QString()
                                : stats.issues.constFirst().technicalDetail));
    QVERIFY(stats.stabilityVerified);
    QCOMPARE(stats.fileCount, quint64(1));
    QCOMPARE(stats.totalSize, quint64(32));
}

void OrphanDetectionTest::pathPresenceReaderDistinguishesDirectoryAndMissingPath()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const auto existing =
            wam::platform::windows::PathPresenceReader::read(temporary.path());
    QVERIFY(existing.supported);
    QCOMPARE(existing.state, wam::platform::windows::PathPresenceState::Present);
    QVERIFY(existing.directory);

    const QString missingPath = QDir(temporary.path()).filePath(
            QStringLiteral("missing/subdirectory"));
    const auto missing =
            wam::platform::windows::PathPresenceReader::read(missingPath);
    QVERIFY(missing.supported);
    QCOMPARE(missing.state, wam::platform::windows::PathPresenceState::Missing);
    QVERIFY(!missing.directory);
}

void OrphanDetectionTest::invalidWindowsPathIsUnavailable()
{
#ifndef Q_OS_WIN
    QSKIP("该错误映射只适用于 Windows Win32 路径状态读取器");
#else
    const auto invalid = wam::platform::windows::PathPresenceReader::read(
            QStringLiteral("C:/WAM_TEST_INVALID_<>/item"));
    QVERIFY(invalid.supported);
    QCOMPARE(invalid.state,
             wam::platform::windows::PathPresenceState::Unavailable);
    QVERIFY(!invalid.technicalDetail.isEmpty());
#endif
}

void OrphanDetectionTest::appResolverMarksUnavailableDeclaredLocationIncomplete()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    QVERIFY(QDir().mkpath(QDir(local).filePath(QStringLiteral("Sample/App Data"))));
    writeFile(QDir(local).filePath(QStringLiteral("Sample/Unavailable")),
              QByteArrayLiteral("not a directory"));

    QJsonObject rule = ruleObject(QStringLiteral("exclusive"));
    rule.insert(QStringLiteral("locations"), QJsonArray {
        QJsonObject {
            {QStringLiteral("scope"), QStringLiteral("local")},
            {QStringLiteral("path"), QStringLiteral("Sample/App Data")},
            {QStringLiteral("ownership"), QStringLiteral("exclusive")}
        },
        QJsonObject {
            {QStringLiteral("scope"), QStringLiteral("local")},
            {QStringLiteral("path"), QStringLiteral("Sample/Unavailable")},
            {QStringLiteral("ownership"), QStringLiteral("exclusive")}
        }
    });
    const auto catalog = catalogFor(rule);
    QVERIFY2(catalog.issues().isEmpty(), qPrintable(issueText(catalog.issues())));

    const QVector<wam::core::ScanTarget> targets =
            wam::core::AppResolver(catalog).discoverTargets({local});
    const wam::core::ScanTarget *target = sampleTarget(targets);
    QVERIFY(target);
    QVERIFY(!target->locationDiscoveryComplete);
    QVERIFY(hasEvidence(target->application, wam::EvidenceSource::Folder,
                        wam::EvidenceStatus::Unavailable));
}

void OrphanDetectionTest::appResolverMarksMissingScopeIncomplete()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    QVERIFY(QDir().mkpath(QDir(local).filePath(QStringLiteral("Sample/App Data"))));

    QJsonObject rule = ruleObject(QStringLiteral("exclusive"));
    rule.insert(QStringLiteral("locations"), QJsonArray {
        QJsonObject {
            {QStringLiteral("scope"), QStringLiteral("local")},
            {QStringLiteral("path"), QStringLiteral("Sample/App Data")},
            {QStringLiteral("ownership"), QStringLiteral("exclusive")}
        },
        QJsonObject {
            {QStringLiteral("scope"), QStringLiteral("roaming")},
            {QStringLiteral("path"), QStringLiteral("Sample/Roaming Data")},
            {QStringLiteral("ownership"), QStringLiteral("exclusive")}
        }
    });
    const auto catalog = catalogFor(rule);
    QVERIFY2(catalog.issues().isEmpty(), qPrintable(issueText(catalog.issues())));

    const QVector<wam::core::ScanTarget> targets =
            wam::core::AppResolver(catalog).discoverTargets({local});
    const wam::core::ScanTarget *target = sampleTarget(targets);
    QVERIFY(target);
    QVERIFY(!target->locationDiscoveryComplete);
    QVERIFY(std::any_of(
            target->application.evidence.cbegin(), target->application.evidence.cend(),
            [](const wam::EvidenceInfo &item) {
        return item.source == wam::EvidenceSource::Folder
                && item.status == wam::EvidenceStatus::Unavailable
                && item.detail.contains(QStringLiteral("roaming"));
    }));
}

void OrphanDetectionTest::appResolverPublishesInstallPathEvidence_data()
{
    QTest::addColumn<int>("pathStateValue");
    QTest::addColumn<bool>("directory");
    QTest::addColumn<int>("expectedStatusValue");
    QTest::newRow("missing")
            << static_cast<int>(wam::InstallationPathState::Missing)
            << false
            << static_cast<int>(wam::EvidenceStatus::NotFound);
    QTest::newRow("present")
            << static_cast<int>(wam::InstallationPathState::Present)
            << true
            << static_cast<int>(wam::EvidenceStatus::Matched);
}

void OrphanDetectionTest::appResolverPublishesInstallPathEvidence()
{
    QFETCH(int, pathStateValue);
    QFETCH(bool, directory);
    QFETCH(int, expectedStatusValue);

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    QVERIFY(QDir().mkpath(QDir(local).filePath(QStringLiteral("Sample/App Data"))));

    const auto catalog = catalogFor(ruleObject(QStringLiteral("exclusive")));
    QVERIFY2(catalog.issues().isEmpty(), qPrintable(issueText(catalog.issues())));

    wam::InstallationEvidenceSnapshot evidenceSnapshot;
    evidenceSnapshot.installPaths.availability =
            wam::InstallationEvidenceAvailability::Complete;
    evidenceSnapshot.installPaths.records.append({
        QStringLiteral("C:/WAM_TEST_MISSING/sample"),
        static_cast<wam::InstallationPathState>(pathStateValue),
        directory
    });

    const QVector<wam::core::ScanTarget> targets =
            wam::core::AppResolver(catalog, evidenceSnapshot).discoverTargets({local});
    const wam::core::ScanTarget *target = sampleTarget(targets);
    QVERIFY(target);
    QCOMPARE(target->locationOwnership, wam::RuleLocationOwnership::Exclusive);
    QVERIFY(hasEvidence(target->application,
                        wam::EvidenceSource::InstallPath,
                        static_cast<wam::EvidenceStatus>(expectedStatusValue)));
}

QTEST_GUILESS_MAIN(OrphanDetectionTest)

#include "tst_orphan_detection.moc"
