#include "src/core/resolver/AppResolver.h"
#include "src/core/rules/RuleCatalog.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

namespace {

QJsonObject evidenceRule(const QJsonObject &identifiers = {},
                         const QString &executablePath =
                                 QStringLiteral("%WAM_TEST_MISSING%/sample.exe"))
{
    QJsonObject rule {
        {QStringLiteral("id"), QStringLiteral("sample-app")},
        {QStringLiteral("version"), QStringLiteral("1")},
        {QStringLiteral("name"), QStringLiteral("Sample App")},
        {QStringLiteral("publisher"), QStringLiteral("Sample Publisher")},
        {QStringLiteral("applicationCategory"), QStringLiteral("工具")},
        {QStringLiteral("executablePath"), executablePath},
        {QStringLiteral("installPath"), QStringLiteral("%WAM_TEST_MISSING%/sample")},
        {QStringLiteral("locations"), QJsonArray {
             QJsonObject {
                 {QStringLiteral("scope"), QStringLiteral("local")},
                 {QStringLiteral("path"), QStringLiteral("Sample/App Data")}
             }
         }},
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
    if (!identifiers.isEmpty())
        rule.insert(QStringLiteral("identifiers"), identifiers);
    return rule;
}

wam::core::rules::RuleCatalog catalogFor(const QJsonObject &rule)
{
    return wam::core::rules::RuleCatalog::fromJsonDocuments({
        {QStringLiteral("evidence-test.json"),
         QJsonDocument(rule).toJson(QJsonDocument::Compact)}
    });
}

const wam::core::ScanTarget *sampleTarget(const QVector<wam::core::ScanTarget> &targets)
{
    const auto iterator = std::find_if(targets.cbegin(), targets.cend(), [](const auto &target) {
        return target.application.id == QStringLiteral("sample-app");
    });
    return iterator == targets.cend() ? nullptr : &*iterator;
}

bool hasEvidence(const wam::ApplicationInfo &application,
                 wam::EvidenceSource source,
                 wam::EvidenceStatus status)
{
    return std::any_of(application.evidence.cbegin(), application.evidence.cend(),
                       [source, status](const wam::EvidenceInfo &item) {
        return item.source == source && item.status == status;
    });
}

QJsonObject registryIdentifiers()
{
    return {
        {QStringLiteral("registryDisplayNames"), QJsonArray {QStringLiteral("Sample App")}},
        {QStringLiteral("registryPublishers"), QJsonArray {QStringLiteral("Sample Publisher")}}
    };
}

} // namespace

class InstallationEvidenceTest final : public QObject {
    Q_OBJECT

private slots:
    void completePartialAndUnavailableRemainDistinct();
    void partialSnapshotCanKeepPositiveEvidence();
    void duplicateRecordsAreOrderIndependent();
    void multipleInstallPathsAreAmbiguous();
    void identityMatchingRejectsFuzzyValues();
    void appxMatchingUsesPackageIdentityOnly();
    void executableOnlyEvidenceMarksInstalled();
};

void InstallationEvidenceTest::completePartialAndUnavailableRemainDistinct()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    QVERIFY(QDir().mkpath(QDir(local).filePath(QStringLiteral("Sample/App Data"))));
    const auto catalog = catalogFor(evidenceRule(registryIdentifiers()));
    QVERIFY(catalog.issues().isEmpty());

    wam::InstallationEvidenceSnapshot complete;
    complete.registry.availability = wam::InstallationEvidenceAvailability::Complete;
    const auto completeTargets = wam::core::AppResolver(catalog, complete).discoverTargets({local});
    const auto *completeTarget = sampleTarget(completeTargets);
    QVERIFY(completeTarget);
    QCOMPARE(completeTarget->application.installState, wam::InstallState::Unknown);
    QCOMPARE(completeTarget->application.confidence, 72);
    QVERIFY(hasEvidence(completeTarget->application, wam::EvidenceSource::Registry,
                        wam::EvidenceStatus::NotFound));

    wam::InstallationEvidenceSnapshot partial;
    partial.registry.availability = wam::InstallationEvidenceAvailability::Partial;
    partial.registry.issues.append(QStringLiteral("一个注册表视图无法读取"));
    const auto partialTargets = wam::core::AppResolver(catalog, partial).discoverTargets({local});
    const auto *partialTarget = sampleTarget(partialTargets);
    QVERIFY(partialTarget);
    QCOMPARE(partialTarget->application.installState, wam::InstallState::Unknown);
    QCOMPARE(partialTarget->application.confidence, 72);
    QVERIFY(hasEvidence(partialTarget->application, wam::EvidenceSource::Registry,
                        wam::EvidenceStatus::Incomplete));

    const auto unavailableTargets = wam::core::AppResolver(catalog, {})
                                            .discoverTargets({local});
    const auto *unavailableTarget = sampleTarget(unavailableTargets);
    QVERIFY(unavailableTarget);
    QCOMPARE(unavailableTarget->application.installState, wam::InstallState::Unknown);
    QCOMPARE(unavailableTarget->application.confidence, 72);
    QVERIFY(hasEvidence(unavailableTarget->application, wam::EvidenceSource::Registry,
                        wam::EvidenceStatus::Unavailable));

    for (const auto *target : {completeTarget, partialTarget, unavailableTarget})
        QVERIFY(target->application.installState != wam::InstallState::PotentialOrphan);
}

void InstallationEvidenceTest::partialSnapshotCanKeepPositiveEvidence()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    QVERIFY(QDir().mkpath(QDir(local).filePath(QStringLiteral("Sample/App Data"))));
    const auto catalog = catalogFor(evidenceRule(registryIdentifiers()));

    wam::InstallationEvidenceSnapshot evidence;
    evidence.registry.availability = wam::InstallationEvidenceAvailability::Partial;
    evidence.registry.issues.append(QStringLiteral("HKLM 32 位视图不可用"));
    evidence.registry.records.append({
        QStringLiteral("HKCU|64|sample"),
        QStringLiteral("Sample App"),
        QStringLiteral("Sample Publisher"),
        QStringLiteral("C:/Apps/Sample")
    });

    const auto targets = wam::core::AppResolver(catalog, evidence).discoverTargets({local});
    const auto *target = sampleTarget(targets);
    QVERIFY(target);
    QCOMPARE(target->application.installState, wam::InstallState::Installed);
    QCOMPARE(target->application.confidence, 94);
    QVERIFY(hasEvidence(target->application, wam::EvidenceSource::Registry,
                        wam::EvidenceStatus::Matched));
    QVERIFY(hasEvidence(target->application, wam::EvidenceSource::Registry,
                        wam::EvidenceStatus::Incomplete));
}

void InstallationEvidenceTest::duplicateRecordsAreOrderIndependent()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    QVERIFY(QDir().mkpath(QDir(local).filePath(QStringLiteral("Sample/App Data"))));
    const auto catalog = catalogFor(evidenceRule(registryIdentifiers()));

    const wam::RegistryInstallationRecord first {
        QStringLiteral("HKCU|64|sample"), QStringLiteral("Sample App"),
        QStringLiteral("Sample Publisher"), QStringLiteral("C:/Apps/Sample")
    };
    const wam::RegistryInstallationRecord duplicate {
        QStringLiteral("HKCU|32|sample"), QStringLiteral(" sample app "),
        QStringLiteral("sample publisher"), QStringLiteral("c:\\apps\\sample")
    };

    wam::InstallationEvidenceSnapshot forward;
    forward.registry.availability = wam::InstallationEvidenceAvailability::Complete;
    forward.registry.records = {first, duplicate};
    wam::InstallationEvidenceSnapshot reverse;
    reverse.registry.availability = wam::InstallationEvidenceAvailability::Complete;
    reverse.registry.records = {duplicate, first};

    const auto forwardTargets = wam::core::AppResolver(catalog, forward).discoverTargets({local});
    const auto reverseTargets = wam::core::AppResolver(catalog, reverse).discoverTargets({local});
    const auto *forwardTarget = sampleTarget(forwardTargets);
    const auto *reverseTarget = sampleTarget(reverseTargets);
    QVERIFY(forwardTarget);
    QVERIFY(reverseTarget);
    QCOMPARE(forwardTarget->application.installState, reverseTarget->application.installState);
    QCOMPARE(forwardTarget->application.confidence, reverseTarget->application.confidence);
    QCOMPARE(forwardTarget->application.installPath, reverseTarget->application.installPath);
    QCOMPARE(forwardTarget->application.evidence.size(), reverseTarget->application.evidence.size());
    for (qsizetype index = 0; index < forwardTarget->application.evidence.size(); ++index) {
        const auto &left = forwardTarget->application.evidence.at(index);
        const auto &right = reverseTarget->application.evidence.at(index);
        QCOMPARE(left.source, right.source);
        QCOMPARE(left.status, right.status);
        QCOMPARE(left.detail, right.detail);
    }
    QVERIFY(hasEvidence(forwardTarget->application, wam::EvidenceSource::Registry,
                        wam::EvidenceStatus::Matched));
    QVERIFY(!hasEvidence(forwardTarget->application, wam::EvidenceSource::Registry,
                         wam::EvidenceStatus::Ambiguous));
}

void InstallationEvidenceTest::multipleInstallPathsAreAmbiguous()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    QVERIFY(QDir().mkpath(QDir(local).filePath(QStringLiteral("Sample/App Data"))));
    const auto catalog = catalogFor(evidenceRule(registryIdentifiers()));

    wam::InstallationEvidenceSnapshot evidence;
    evidence.registry.availability = wam::InstallationEvidenceAvailability::Complete;
    evidence.registry.records = {
        {QStringLiteral("HKCU|sample"), QStringLiteral("Sample App"),
         QStringLiteral("Sample Publisher"), QStringLiteral("C:/Users/Me/Sample")},
        {QStringLiteral("HKLM|sample"), QStringLiteral("Sample App"),
         QStringLiteral("Sample Publisher"), QStringLiteral("C:/Program Files/Sample")}
    };

    const auto targets = wam::core::AppResolver(catalog, evidence).discoverTargets({local});
    const auto *target = sampleTarget(targets);
    QVERIFY(target);
    QCOMPARE(target->application.installState, wam::InstallState::Installed);
    QCOMPARE(target->application.confidence, 90);
    QVERIFY(hasEvidence(target->application, wam::EvidenceSource::Registry,
                        wam::EvidenceStatus::Ambiguous));
    QVERIFY(target->application.installPath.contains(QStringLiteral("WAM_TEST_MISSING")));
}

void InstallationEvidenceTest::identityMatchingRejectsFuzzyValues()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    QVERIFY(QDir().mkpath(QDir(local).filePath(QStringLiteral("Sample/App Data"))));
    const auto catalog = catalogFor(evidenceRule(registryIdentifiers()));

    wam::InstallationEvidenceSnapshot evidence;
    evidence.registry.availability = wam::InstallationEvidenceAvailability::Complete;
    evidence.registry.records = {
        {QStringLiteral("preview"), QStringLiteral("Sample App Preview"),
         QStringLiteral("Sample Publisher"), {}},
        {QStringLiteral("spaces"), QStringLiteral("Sample  App"),
         QStringLiteral("Sample Publisher"), {}},
        {QStringLiteral("version"), QStringLiteral("Sample App 1.0"),
         QStringLiteral("Sample Publisher"), {}}
    };

    const auto targets = wam::core::AppResolver(catalog, evidence).discoverTargets({local});
    const auto *target = sampleTarget(targets);
    QVERIFY(target);
    QCOMPARE(target->application.installState, wam::InstallState::Unknown);
    QCOMPARE(target->application.confidence, 72);
    QVERIFY(hasEvidence(target->application, wam::EvidenceSource::Registry,
                        wam::EvidenceStatus::NotFound));
}

void InstallationEvidenceTest::appxMatchingUsesPackageIdentityOnly()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    QVERIFY(QDir().mkpath(QDir(local).filePath(QStringLiteral("Sample/App Data"))));
    const QJsonObject identifiers {
        {QStringLiteral("appxPackageNames"), QJsonArray {QStringLiteral("Sample.Package")}},
        {QStringLiteral("appxPublishers"), QJsonArray {QStringLiteral("CN=Sample")}}
    };
    const auto catalog = catalogFor(evidenceRule(identifiers));

    wam::InstallationEvidenceSnapshot evidence;
    evidence.appx.availability = wam::InstallationEvidenceAvailability::Complete;
    evidence.appx.records.append({
        QStringLiteral("Different.Package"),
        QStringLiteral("CN=Sample"),
        QStringLiteral("Sample.Package_family"),
        QStringLiteral("Sample.Package"),
        QStringLiteral("C:/Program Files/WindowsApps/Different.Package")
    });

    const auto targets = wam::core::AppResolver(catalog, evidence).discoverTargets({local});
    const auto *target = sampleTarget(targets);
    QVERIFY(target);
    QCOMPARE(target->application.installState, wam::InstallState::Unknown);
    QCOMPARE(target->application.confidence, 72);
    QVERIFY(hasEvidence(target->application, wam::EvidenceSource::Appx,
                        wam::EvidenceStatus::NotFound));
}

void InstallationEvidenceTest::executableOnlyEvidenceMarksInstalled()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    QVERIFY(QDir().mkpath(QDir(local).filePath(QStringLiteral("Sample/App Data"))));
    const QString executable = QDir(temporary.path()).filePath(QStringLiteral("sample.exe"));
    QFile file(executable);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("test"), qint64(4));
    file.close();

    const auto catalog = catalogFor(evidenceRule({}, executable));
    const auto targets = wam::core::AppResolver(catalog).discoverTargets({local});
    const auto *target = sampleTarget(targets);
    QVERIFY(target);
    QCOMPARE(target->application.installState, wam::InstallState::Installed);
    QCOMPARE(target->application.confidence, 92);
    QVERIFY(hasEvidence(target->application, wam::EvidenceSource::Executable,
                        wam::EvidenceStatus::Matched));
    QVERIFY(target->application.installState != wam::InstallState::PotentialOrphan);
}

QTEST_GUILESS_MAIN(InstallationEvidenceTest)

#include "tst_installation_evidence.moc"
