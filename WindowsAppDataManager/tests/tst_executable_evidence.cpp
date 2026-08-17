#include "src/core/resolver/AppResolver.h"
#include "src/core/rules/RuleCatalog.h"
#include "src/core/rules/RuleLoader.h"
#include "src/core/rules/RulePathResolver.h"
#include "src/services/InstallationEvidenceCollector.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

namespace {

QJsonObject ruleObject(const QString &executablePath,
                       const QJsonObject &identifiers = {})
{
    QJsonObject rule {
        {QStringLiteral("id"), QStringLiteral("sample-app")},
        {QStringLiteral("version"), QStringLiteral("1")},
        {QStringLiteral("name"), QStringLiteral("Sample App")},
        {QStringLiteral("publisher"), QStringLiteral("Sample Publisher")},
        {QStringLiteral("applicationCategory"), QStringLiteral("工具")},
        {QStringLiteral("executablePath"), executablePath},
        {QStringLiteral("installPath"), QStringLiteral("C:/Apps/Sample")},
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

QJsonObject executableIdentifiers(bool includeAuthenticode = true)
{
    QJsonObject identifiers {
        {QStringLiteral("executableProductNames"),
         QJsonArray {QStringLiteral("Sample Product")}},
        {QStringLiteral("executableCompanyNames"),
         QJsonArray {QStringLiteral("Sample Company")}},
        {QStringLiteral("executableOriginalFilenames"),
         QJsonArray {QStringLiteral("sample.exe")}}
    };
    if (includeAuthenticode) {
        identifiers.insert(
                QStringLiteral("authenticodePublishers"),
                QJsonArray {QStringLiteral("Sample Publisher")});
    }
    return identifiers;
}

wam::core::rules::RuleCatalog catalogFor(const QJsonObject &rule)
{
    return wam::core::rules::RuleCatalog::fromJsonDocuments({
        {QStringLiteral("executable-evidence-test.json"),
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

bool hasRuleIssue(const QVector<wam::RuleLoadIssue> &issues,
                  wam::RuleIssueCode code,
                  const QString &field)
{
    return std::any_of(issues.cbegin(), issues.cend(),
                       [code, &field](const wam::RuleLoadIssue &issue) {
        return issue.code == code && issue.field.contains(field);
    });
}

wam::ExecutableEvidenceRecord exactRecord(const QString &path)
{
    wam::ExecutableEvidenceRecord record;
    record.path = path;
    record.pathState = wam::ExecutablePathState::Present;
    record.metadataState = wam::VersionMetadataState::Available;
    record.productName = QStringLiteral("Sample Product");
    record.companyName = QStringLiteral("Sample Company");
    record.fileDescription = QStringLiteral("Sample Application");
    record.originalFilename = QStringLiteral("sample.exe");
    record.authenticodeState = wam::AuthenticodeState::Trusted;
    record.signerPublisher = QStringLiteral("Sample Publisher");
    return record;
}

QString createDataRoot(QTemporaryDir &temporary)
{
    const QString local = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    if (!QDir().mkpath(QDir(local).filePath(QStringLiteral("Sample/App Data"))))
        return {};
    return local;
}

class ScopedEnvironmentVariableUnset final {
public:
    explicit ScopedEnvironmentVariableUnset(QByteArray name)
        : m_name(std::move(name)),
          m_wasSet(qEnvironmentVariableIsSet(m_name.constData())),
          m_previousValue(qgetenv(m_name.constData()))
    {
        qunsetenv(m_name.constData());
    }

    ~ScopedEnvironmentVariableUnset()
    {
        if (m_wasSet)
            qputenv(m_name.constData(), m_previousValue);
        else
            qunsetenv(m_name.constData());
    }

private:
    QByteArray m_name;
    bool m_wasSet = false;
    QByteArray m_previousValue;
};

class ScopedEnvironmentVariableValue final {
public:
    ScopedEnvironmentVariableValue(QByteArray name, const QByteArray &value)
        : m_name(std::move(name)),
          m_wasSet(qEnvironmentVariableIsSet(m_name.constData())),
          m_previousValue(qgetenv(m_name.constData()))
    {
        qputenv(m_name.constData(), value);
    }

    ~ScopedEnvironmentVariableValue()
    {
        if (m_wasSet)
            qputenv(m_name.constData(), m_previousValue);
        else
            qunsetenv(m_name.constData());
    }

private:
    QByteArray m_name;
    bool m_wasSet = false;
    QByteArray m_previousValue;
};

} // namespace

class ExecutableEvidenceTest final : public QObject {
    Q_OBJECT

private slots:
    void ruleLoaderAcceptsExecutableIdentifiers();
    void ruleLoaderRejectsMalformedExecutableIdentifiers();
    void ruleLoaderRejectsUnsafeAbsoluteRulePaths_data();
    void ruleLoaderRejectsUnsafeAbsoluteRulePaths();
    void resolveRulePathRejectsUnsafeEnvironmentRoots_data();
    void resolveRulePathRejectsUnsafeEnvironmentRoots();
    void normalizedPathKeyRejectsUnsafeForms();
    void ruleCatalogRejectsDuplicateExecutablePathClaims();
    void missingEnvironmentVariableProducesUnavailableEvidence();
    void resolverAcceptsExactMetadataAndPublisher();
    void resolverRejectsMetadataAndPublisherConflicts();
    void resolverAcceptsUnsignedExactMetadataWhenSignatureIsNotRequired();
    void resolverDoesNotPromoteUnclaimedTrustedPublisher();
    void resolverTreatsMissingMetadataFieldsAsIncomplete();
    void resolverDoesNotLetSignerBypassMissingMetadata();
    void resolverDoesNotTrustInvalidSignature();
    void missingUnavailableAndWrongPathRemainUnknown();
};

void ExecutableEvidenceTest::ruleLoaderAcceptsExecutableIdentifiers()
{
    QJsonObject identifiers {
        {QStringLiteral("executableProductNames"),
         QJsonArray {QStringLiteral("  Sample Product  ")}},
        {QStringLiteral("executableCompanyNames"),
         QJsonArray {QStringLiteral(" Sample Company ")}},
        {QStringLiteral("executableOriginalFilenames"),
         QJsonArray {QStringLiteral(" sample.exe ")}},
        {QStringLiteral("authenticodePublishers"),
         QJsonArray {QStringLiteral(" Sample Publisher ")}}
    };
    const auto result = wam::core::rules::RuleLoader::load(
            QJsonDocument(ruleObject(QStringLiteral("C:/Apps/Sample/sample.exe"),
                                     identifiers))
                    .toJson(QJsonDocument::Compact),
            QStringLiteral("executable-identifiers.json"));

    QVERIFY(result.isValid());
    QVERIFY(result.rule.has_value());
    QCOMPARE(result.rule->identifiers.executableProductNames,
             QStringList {QStringLiteral("Sample Product")});
    QCOMPARE(result.rule->identifiers.executableCompanyNames,
             QStringList {QStringLiteral("Sample Company")});
    QCOMPARE(result.rule->identifiers.executableOriginalFilenames,
             QStringList {QStringLiteral("sample.exe")});
    QCOMPARE(result.rule->identifiers.authenticodePublishers,
             QStringList {QStringLiteral("Sample Publisher")});
}

void ExecutableEvidenceTest::ruleLoaderRejectsMalformedExecutableIdentifiers()
{
    QJsonObject identifiers {
        {QStringLiteral("executableProductNames"), QJsonArray {}},
        {QStringLiteral("authenticodePublishers"), QJsonArray {
             QStringLiteral("Sample Publisher"),
             QStringLiteral(" sample publisher ")
         }}
    };
    const auto result = wam::core::rules::RuleLoader::load(
            QJsonDocument(ruleObject(QStringLiteral("C:/Apps/Sample/sample.exe"),
                                     identifiers))
                    .toJson(QJsonDocument::Compact),
            QStringLiteral("invalid-executable-identifiers.json"));

    QVERIFY(!result.rule.has_value());
    QVERIFY(hasRuleIssue(result.issues, wam::RuleIssueCode::InvalidValue,
                         QStringLiteral("identifiers.executableProductNames")));
    QVERIFY(hasRuleIssue(result.issues, wam::RuleIssueCode::InvalidValue,
                         QStringLiteral("identifiers.authenticodePublishers[1]")));
}

void ExecutableEvidenceTest::ruleLoaderRejectsUnsafeAbsoluteRulePaths_data()
{
    QTest::addColumn<QString>("path");

    QTest::newRow("unknown-variable") << QStringLiteral("%UNKNOWN_ROOT%/Sample/app.exe");
    QTest::newRow("unmatched-variable") << QStringLiteral("%LOCALAPPDATA/Sample/app.exe");
    QTest::newRow("relative") << QStringLiteral("Sample/app.exe");
    QTest::newRow("drive-relative") << QStringLiteral("C:Sample/app.exe");
    QTest::newRow("unicode-drive-letter") << QStringLiteral("é:/Apps/app.exe");
    QTest::newRow("parent-traversal") << QStringLiteral("C:/Apps/../escape.exe");
    QTest::newRow("variable-parent-traversal")
            << QStringLiteral("%LOCALAPPDATA%/Sample/../../escape.exe");
    QTest::newRow("unc") << QStringLiteral("//server/share/app.exe");
    QTest::newRow("device") << QStringLiteral("\\\\?\\C:\\Apps\\app.exe");
    QTest::newRow("device-dot") << QStringLiteral("\\\\.\\C:\\Apps\\app.exe");
    QTest::newRow("native-device") << QStringLiteral("\\??\\C:\\Apps\\app.exe");
    QTest::newRow("lowercase-variable")
            << QStringLiteral("%localappdata%/Sample/app.exe");
    QTest::newRow("dos-device-nul") << QStringLiteral("C:/NUL/app.exe");
    QTest::newRow("dos-device-com1-extension")
            << QStringLiteral("C:/Apps/COM1.exe");
    QTest::newRow("dos-device-variable-root")
            << QStringLiteral("%LOCALAPPDATA%/NUL/app.exe");
    QTest::newRow("parent-with-trailing-space")
            << QStringLiteral("C:/Apps/.. /Windows/app.exe");
    QTest::newRow("segment-with-trailing-dot")
            << QStringLiteral("C:/Apps/Folder./app.exe");
    QTest::newRow("path-with-trailing-space")
            << QStringLiteral("C:/Apps/app.exe ");
    QTest::newRow("alternate-data-stream")
            << QStringLiteral("C:/Apps/app.exe:stream");
}

void ExecutableEvidenceTest::ruleLoaderRejectsUnsafeAbsoluteRulePaths()
{
    QFETCH(QString, path);
    QString validationError;
    QVERIFY2(!wam::core::rules::validateRulePath(path, &validationError),
             qPrintable(QStringLiteral("路径被错误接受：%1").arg(path)));

    for (const QString &field : {QStringLiteral("executablePath"),
                                 QStringLiteral("installPath")}) {
        QJsonObject document = ruleObject(QStringLiteral("C:/Apps/Sample/sample.exe"));
        document.insert(field, path);
        const auto result = wam::core::rules::RuleLoader::load(
                QJsonDocument(document).toJson(QJsonDocument::Compact),
                QStringLiteral("unsafe-path.json"));
        QVERIFY2(!result.rule.has_value(), qPrintable(field));
        QVERIFY2(hasRuleIssue(result.issues, wam::RuleIssueCode::UnsafePath, field),
                 qPrintable(field));
    }
}

void ExecutableEvidenceTest::resolveRulePathRejectsUnsafeEnvironmentRoots_data()
{
    QTest::addColumn<QString>("environmentRoot");

    QTest::newRow("extended-device")
            << QStringLiteral("\\\\?\\C:\\Users\\RuleTest");
    QTest::newRow("win32-device")
            << QStringLiteral("\\\\.\\C:\\Users\\RuleTest");
    QTest::newRow("unc")
            << QStringLiteral("\\\\server\\share");
    QTest::newRow("native-device")
            << QStringLiteral("\\??\\C:\\Users\\RuleTest");
    QTest::newRow("relative-drive")
            << QStringLiteral("C:Users\\RuleTest");
    QTest::newRow("unicode-drive-letter")
            << QStringLiteral("é:\\Users\\RuleTest");
    QTest::newRow("parent-traversal")
            << QStringLiteral("C:\\Users\\RuleTest\\..\\Windows");
    QTest::newRow("reserved-device")
            << QStringLiteral("C:\\Users\\NUL\\RuleTest");
    QTest::newRow("trailing-dot")
            << QStringLiteral("C:\\Users\\RuleTest.\\Data");
    QTest::newRow("unexpanded-variable")
            << QStringLiteral("C:\\Users\\%UNEXPANDED%\\Data");
}

void ExecutableEvidenceTest::resolveRulePathRejectsUnsafeEnvironmentRoots()
{
    QFETCH(QString, environmentRoot);
    ScopedEnvironmentVariableValue overrideUserProfile(
            "USERPROFILE", environmentRoot.toUtf8());
    const QString effectiveRoot = qEnvironmentVariable("USERPROFILE");
    QVERIFY(!effectiveRoot.isEmpty());
    if (environmentRoot.front().unicode() < 128) {
        QCOMPARE(effectiveRoot, environmentRoot);
    } else {
        QVERIFY(effectiveRoot.front().isLetter());
    }

    const QString rulePath = QStringLiteral("%USERPROFILE%/Sample/app.exe");
    const auto resolution = wam::core::rules::resolveRulePath(rulePath);
    QCOMPARE(resolution.status,
             wam::core::rules::RulePathResolutionStatus::Invalid);
    QVERIFY(resolution.path.isEmpty());

    const auto catalog = catalogFor(ruleObject(rulePath));
    QVERIFY(catalog.issues().isEmpty());
    const auto evidence = wam::services::InstallationEvidenceCollector::collect(catalog);
    QCOMPARE(evidence.executable.records.size(), 1);
    QCOMPARE(evidence.executable.records.constFirst().pathState,
             wam::ExecutablePathState::Unavailable);
    QCOMPARE(evidence.executable.records.constFirst().metadataState,
             wam::VersionMetadataState::Unavailable);
    QCOMPARE(evidence.executable.records.constFirst().authenticodeState,
             wam::AuthenticodeState::Unavailable);
}

void ExecutableEvidenceTest::normalizedPathKeyRejectsUnsafeForms()
{
#ifndef Q_OS_WIN
    QSKIP("Windows 路径归一化仅在 Windows 上验证");
#else
    QVERIFY(wam::core::rules::normalizedPathKey(
                    QStringLiteral("\\\\?\\C:\\Apps\\app.exe")).isEmpty());
    QVERIFY(wam::core::rules::normalizedPathKey(
                    QStringLiteral("C:\\Apps\\..\\escape.exe")).isEmpty());
    QVERIFY(wam::core::rules::normalizedPathKey(
                    QStringLiteral("C:/Apps/./app.exe")).isEmpty());
    QVERIFY(wam::core::rules::normalizedPathKey(
                    QStringLiteral("C:/Apps/COM1.exe")).isEmpty());
    QVERIFY(wam::core::rules::normalizedPathKey(
                    QStringLiteral("C:/Apps/app.exe ")).isEmpty());
    QVERIFY(wam::core::rules::normalizedPathKey(
                    QStringLiteral("é:/Apps/app.exe")).isEmpty());

    QCOMPARE(wam::core::rules::normalizedPathKey(
                     QStringLiteral("C:\\Apps\\Sample.exe")),
             wam::core::rules::normalizedPathKey(
                     QStringLiteral("c:/apps/SAMPLE.EXE")));
#endif
}

void ExecutableEvidenceTest::ruleCatalogRejectsDuplicateExecutablePathClaims()
{
    QJsonObject first = ruleObject(
            QStringLiteral("%LOCALAPPDATA%/Sample/app.exe"));
    first.insert(QStringLiteral("id"), QStringLiteral("first-app"));

    QJsonObject duplicate = ruleObject(
            QStringLiteral("%LOCALAPPDATA%\\sample\\APP.EXE"));
    duplicate.insert(QStringLiteral("id"), QStringLiteral("duplicate-app"));

    const auto duplicateCatalog = wam::core::rules::RuleCatalog::fromJsonDocuments({
        {QStringLiteral("first.json"),
         QJsonDocument(first).toJson(QJsonDocument::Compact)},
        {QStringLiteral("duplicate.json"),
         QJsonDocument(duplicate).toJson(QJsonDocument::Compact)}
    });
    QCOMPARE(duplicateCatalog.applications().size(), 1);
    QVERIFY(hasRuleIssue(duplicateCatalog.issues(),
                         wam::RuleIssueCode::AmbiguousIdentifier,
                         QStringLiteral("executablePath")));

    QJsonObject distinct = ruleObject(
            QStringLiteral("%LOCALAPPDATA%/Sample/other.exe"));
    distinct.insert(QStringLiteral("id"), QStringLiteral("distinct-app"));
    const auto distinctCatalog = wam::core::rules::RuleCatalog::fromJsonDocuments({
        {QStringLiteral("first.json"),
         QJsonDocument(first).toJson(QJsonDocument::Compact)},
        {QStringLiteral("distinct.json"),
         QJsonDocument(distinct).toJson(QJsonDocument::Compact)}
    });
    QCOMPARE(distinctCatalog.applications().size(), 2);
    QVERIFY(distinctCatalog.issues().isEmpty());

    ScopedEnvironmentVariableValue localAppData(
            "LOCALAPPDATA", QByteArrayLiteral("C:/Users/RuleTest/AppData/Local"));
    ScopedEnvironmentVariableValue userProfile(
            "USERPROFILE", QByteArrayLiteral("C:/Users/RuleTest"));

    QJsonObject variablePath = ruleObject(
            QStringLiteral("%LOCALAPPDATA%/Sample/app.exe"));
    variablePath.insert(QStringLiteral("id"), QStringLiteral("variable-path-app"));
    QJsonObject absolutePath = ruleObject(
            QStringLiteral("C:/Users/RuleTest/AppData/Local/Sample/app.exe"));
    absolutePath.insert(QStringLiteral("id"), QStringLiteral("absolute-path-app"));
    const auto runtimeEquivalentCatalog =
            wam::core::rules::RuleCatalog::fromJsonDocuments({
        {QStringLiteral("variable-path.json"),
         QJsonDocument(variablePath).toJson(QJsonDocument::Compact)},
        {QStringLiteral("absolute-path.json"),
         QJsonDocument(absolutePath).toJson(QJsonDocument::Compact)}
    });
    QCOMPARE(runtimeEquivalentCatalog.applications().size(), 1);
    QVERIFY(hasRuleIssue(runtimeEquivalentCatalog.issues(),
                         wam::RuleIssueCode::AmbiguousIdentifier,
                         QStringLiteral("executablePath")));

    QJsonObject aliasedVariablePath = ruleObject(
            QStringLiteral("%USERPROFILE%/AppData/Local/Sample/app.exe"));
    aliasedVariablePath.insert(QStringLiteral("id"), QStringLiteral("alias-path-app"));
    const auto aliasCatalog = wam::core::rules::RuleCatalog::fromJsonDocuments({
        {QStringLiteral("local-appdata.json"),
         QJsonDocument(variablePath).toJson(QJsonDocument::Compact)},
        {QStringLiteral("user-profile.json"),
         QJsonDocument(aliasedVariablePath).toJson(QJsonDocument::Compact)}
    });
    QCOMPARE(aliasCatalog.applications().size(), 1);
    QVERIFY(hasRuleIssue(aliasCatalog.issues(),
                         wam::RuleIssueCode::AmbiguousIdentifier,
                         QStringLiteral("executablePath")));
}

void ExecutableEvidenceTest::missingEnvironmentVariableProducesUnavailableEvidence()
{
    ScopedEnvironmentVariableUnset unsetUserProfile("USERPROFILE");
    const QString unresolvedPath = QStringLiteral("%USERPROFILE%/Sample/app.exe");
    const auto resolution = wam::core::rules::resolveRulePath(unresolvedPath);
    QCOMPARE(resolution.status,
             wam::core::rules::RulePathResolutionStatus::MissingEnvironmentVariable);
    QVERIFY(resolution.path.isEmpty());
    QVERIFY(wam::core::rules::normalizedPathKey(resolution.path).isEmpty());

    const auto catalog = catalogFor(ruleObject(unresolvedPath));
    QVERIFY(catalog.issues().isEmpty());
    const auto evidence = wam::services::InstallationEvidenceCollector::collect(catalog);
    QCOMPARE(evidence.executable.availability,
             wam::InstallationEvidenceAvailability::Unavailable);
    QCOMPARE(evidence.executable.records.size(), 1);
    QCOMPARE(evidence.executable.records.constFirst().pathState,
             wam::ExecutablePathState::Unavailable);
    QCOMPARE(evidence.executable.records.constFirst().path, unresolvedPath);
    QVERIFY(!evidence.executable.issues.isEmpty());
}

void ExecutableEvidenceTest::resolverAcceptsExactMetadataAndPublisher()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = createDataRoot(temporary);
    QVERIFY(!local.isEmpty());
    const QString executable = QDir(temporary.path()).filePath(QStringLiteral("sample.exe"));
    const auto catalog = catalogFor(ruleObject(executable, executableIdentifiers()));
    QVERIFY(catalog.issues().isEmpty());

    wam::InstallationEvidenceSnapshot evidence;
    evidence.executable.availability = wam::InstallationEvidenceAvailability::Complete;
    evidence.executable.records.append(exactRecord(executable));

    const auto targets = wam::core::AppResolver(catalog, evidence).discoverTargets({local});
    const auto *target = sampleTarget(targets);
    QVERIFY(target);
    QCOMPARE(target->application.installState, wam::InstallState::Installed);
    QCOMPARE(target->application.confidence, 97);
    QVERIFY(hasEvidence(target->application, wam::EvidenceSource::Executable,
                        wam::EvidenceStatus::Matched));
    QVERIFY(hasEvidence(target->application, wam::EvidenceSource::Publisher,
                        wam::EvidenceStatus::Matched));
}

void ExecutableEvidenceTest::resolverRejectsMetadataAndPublisherConflicts()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = createDataRoot(temporary);
    QVERIFY(!local.isEmpty());
    const QString executable = QDir(temporary.path()).filePath(QStringLiteral("sample.exe"));
    const auto catalog = catalogFor(ruleObject(executable, executableIdentifiers()));

    wam::InstallationEvidenceSnapshot metadataConflict;
    metadataConflict.executable.availability =
            wam::InstallationEvidenceAvailability::Complete;
    auto wrongMetadata = exactRecord(executable);
    wrongMetadata.productName = QStringLiteral("Different Product");
    metadataConflict.executable.records.append(wrongMetadata);
    const auto metadataTargets = wam::core::AppResolver(catalog, metadataConflict)
                                         .discoverTargets({local});
    const auto *metadataTarget = sampleTarget(metadataTargets);
    QVERIFY(metadataTarget);
    QCOMPARE(metadataTarget->application.installState, wam::InstallState::Unknown);
    QCOMPARE(metadataTarget->application.confidence, 49);
    QVERIFY(hasEvidence(metadataTarget->application, wam::EvidenceSource::Executable,
                        wam::EvidenceStatus::Conflict));

    wam::InstallationEvidenceSnapshot publisherConflict;
    publisherConflict.executable.availability =
            wam::InstallationEvidenceAvailability::Complete;
    auto wrongPublisher = exactRecord(executable);
    wrongPublisher.signerPublisher = QStringLiteral("Different Publisher");
    publisherConflict.executable.records.append(wrongPublisher);
    const auto publisherTargets = wam::core::AppResolver(catalog, publisherConflict)
                                          .discoverTargets({local});
    const auto *publisherTarget = sampleTarget(publisherTargets);
    QVERIFY(publisherTarget);
    QCOMPARE(publisherTarget->application.installState, wam::InstallState::Unknown);
    QCOMPARE(publisherTarget->application.confidence, 49);
    QVERIFY(hasEvidence(publisherTarget->application, wam::EvidenceSource::Publisher,
                        wam::EvidenceStatus::Conflict));

    QVERIFY(metadataTarget->application.installState != wam::InstallState::PotentialOrphan);
    QVERIFY(publisherTarget->application.installState != wam::InstallState::PotentialOrphan);
}

void ExecutableEvidenceTest::resolverAcceptsUnsignedExactMetadataWhenSignatureIsNotRequired()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = createDataRoot(temporary);
    QVERIFY(!local.isEmpty());
    const QString executable = QDir(temporary.path()).filePath(QStringLiteral("sample.exe"));
    const auto catalog = catalogFor(
            ruleObject(executable, executableIdentifiers(false)));

    wam::InstallationEvidenceSnapshot evidence;
    evidence.executable.availability = wam::InstallationEvidenceAvailability::Complete;
    auto record = exactRecord(executable);
    record.authenticodeState = wam::AuthenticodeState::Unsigned;
    record.signerPublisher.clear();
    evidence.executable.records.append(record);

    const auto targets = wam::core::AppResolver(catalog, evidence).discoverTargets({local});
    const auto *target = sampleTarget(targets);
    QVERIFY(target);
    QCOMPARE(target->application.installState, wam::InstallState::Installed);
    QCOMPARE(target->application.confidence, 88);
    QVERIFY(hasEvidence(target->application, wam::EvidenceSource::Executable,
                        wam::EvidenceStatus::Matched));
    QVERIFY(hasEvidence(target->application, wam::EvidenceSource::Publisher,
                        wam::EvidenceStatus::NotFound));
}

void ExecutableEvidenceTest::resolverDoesNotPromoteUnclaimedTrustedPublisher()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = createDataRoot(temporary);
    QVERIFY(!local.isEmpty());
    const QString executable = QDir(temporary.path()).filePath(QStringLiteral("sample.exe"));
    const auto catalog = catalogFor(
            ruleObject(executable, executableIdentifiers(false)));

    wam::InstallationEvidenceSnapshot evidence;
    evidence.executable.availability = wam::InstallationEvidenceAvailability::Complete;
    evidence.executable.records.append(exactRecord(executable));

    const auto targets = wam::core::AppResolver(catalog, evidence).discoverTargets({local});
    const auto *target = sampleTarget(targets);
    QVERIFY(target);
    QCOMPARE(target->application.installState, wam::InstallState::Installed);
    QCOMPARE(target->application.confidence, 88);
    QVERIFY(hasEvidence(target->application, wam::EvidenceSource::Executable,
                        wam::EvidenceStatus::Matched));
    QVERIFY(hasEvidence(target->application, wam::EvidenceSource::Publisher,
                        wam::EvidenceStatus::Partial));
}

void ExecutableEvidenceTest::resolverTreatsMissingMetadataFieldsAsIncomplete()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = createDataRoot(temporary);
    QVERIFY(!local.isEmpty());
    const QString executable = QDir(temporary.path()).filePath(QStringLiteral("sample.exe"));
    const auto catalog = catalogFor(
            ruleObject(executable, executableIdentifiers(false)));

    wam::InstallationEvidenceSnapshot evidence;
    evidence.executable.availability = wam::InstallationEvidenceAvailability::Complete;
    auto record = exactRecord(executable);
    record.productName.clear();
    record.authenticodeState = wam::AuthenticodeState::Unsigned;
    record.signerPublisher.clear();
    evidence.executable.records.append(record);

    const auto targets = wam::core::AppResolver(catalog, evidence).discoverTargets({local});
    const auto *target = sampleTarget(targets);
    QVERIFY(target);
    QCOMPARE(target->application.installState, wam::InstallState::Unknown);
    QVERIFY(hasEvidence(target->application, wam::EvidenceSource::Executable,
                        wam::EvidenceStatus::Incomplete));
    QVERIFY(!hasEvidence(target->application, wam::EvidenceSource::Executable,
                         wam::EvidenceStatus::Conflict));
    QVERIFY(target->application.installState != wam::InstallState::PotentialOrphan);
}

void ExecutableEvidenceTest::resolverDoesNotLetSignerBypassMissingMetadata()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = createDataRoot(temporary);
    QVERIFY(!local.isEmpty());
    const QString executable = QDir(temporary.path()).filePath(QStringLiteral("sample.exe"));
    const auto catalog = catalogFor(ruleObject(executable, executableIdentifiers()));

    wam::InstallationEvidenceSnapshot evidence;
    evidence.executable.availability = wam::InstallationEvidenceAvailability::Complete;
    auto record = exactRecord(executable);
    record.productName.clear();
    evidence.executable.records.append(record);

    const auto targets = wam::core::AppResolver(catalog, evidence).discoverTargets({local});
    const auto *target = sampleTarget(targets);
    QVERIFY(target);
    QCOMPARE(target->application.installState, wam::InstallState::Unknown);
    QCOMPARE(target->application.confidence, 72);
    QVERIFY(hasEvidence(target->application, wam::EvidenceSource::Executable,
                        wam::EvidenceStatus::Incomplete));
    QVERIFY(hasEvidence(target->application, wam::EvidenceSource::Publisher,
                        wam::EvidenceStatus::Matched));
    QVERIFY(target->application.installState != wam::InstallState::PotentialOrphan);
}

void ExecutableEvidenceTest::resolverDoesNotTrustInvalidSignature()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = createDataRoot(temporary);
    QVERIFY(!local.isEmpty());
    const QString executable = QDir(temporary.path()).filePath(QStringLiteral("sample.exe"));
    const auto catalog = catalogFor(
            ruleObject(executable, executableIdentifiers(false)));

    wam::InstallationEvidenceSnapshot evidence;
    evidence.executable.availability = wam::InstallationEvidenceAvailability::Complete;
    auto record = exactRecord(executable);
    record.authenticodeState = wam::AuthenticodeState::Untrusted;
    evidence.executable.records.append(record);

    const auto targets = wam::core::AppResolver(catalog, evidence).discoverTargets({local});
    const auto *target = sampleTarget(targets);
    QVERIFY(target);
    QCOMPARE(target->application.installState, wam::InstallState::Unknown);
    QVERIFY(target->application.confidence <= 90);
    QVERIFY(hasEvidence(target->application, wam::EvidenceSource::Executable,
                        wam::EvidenceStatus::Matched));
    QVERIFY(hasEvidence(target->application, wam::EvidenceSource::Publisher,
                        wam::EvidenceStatus::Incomplete));
    QVERIFY(target->application.installState != wam::InstallState::PotentialOrphan);
}

void ExecutableEvidenceTest::missingUnavailableAndWrongPathRemainUnknown()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = createDataRoot(temporary);
    QVERIFY(!local.isEmpty());
    const QString executable = QDir(temporary.path()).filePath(QStringLiteral("sample.exe"));
    const auto catalog = catalogFor(ruleObject(executable, executableIdentifiers()));

    wam::InstallationEvidenceSnapshot missing;
    missing.executable.availability = wam::InstallationEvidenceAvailability::Complete;
    auto missingRecord = exactRecord(executable);
    missingRecord.pathState = wam::ExecutablePathState::Missing;
    missingRecord.metadataState = wam::VersionMetadataState::Missing;
    missingRecord.authenticodeState = wam::AuthenticodeState::Unavailable;
    missing.executable.records.append(missingRecord);
    const auto missingTargets = wam::core::AppResolver(catalog, missing)
                                        .discoverTargets({local});
    const auto *missingTarget = sampleTarget(missingTargets);
    QVERIFY(missingTarget);
    QCOMPARE(missingTarget->application.installState, wam::InstallState::Unknown);
    QCOMPARE(missingTarget->application.confidence, 72);
    QVERIFY(hasEvidence(missingTarget->application, wam::EvidenceSource::Executable,
                        wam::EvidenceStatus::NotFound));

    const auto unavailableTargets = wam::core::AppResolver(catalog, {})
                                            .discoverTargets({local});
    const auto *unavailableTarget = sampleTarget(unavailableTargets);
    QVERIFY(unavailableTarget);
    QCOMPARE(unavailableTarget->application.installState, wam::InstallState::Unknown);
    QVERIFY(hasEvidence(unavailableTarget->application, wam::EvidenceSource::Executable,
                        wam::EvidenceStatus::Unavailable));

    wam::InstallationEvidenceSnapshot wrongPath;
    wrongPath.executable.availability = wam::InstallationEvidenceAvailability::Complete;
    wrongPath.executable.records.append(exactRecord(executable + QStringLiteral(".bak")));
    const auto wrongPathTargets = wam::core::AppResolver(catalog, wrongPath)
                                          .discoverTargets({local});
    const auto *wrongPathTarget = sampleTarget(wrongPathTargets);
    QVERIFY(wrongPathTarget);
    QCOMPARE(wrongPathTarget->application.installState, wam::InstallState::Unknown);
    QVERIFY(hasEvidence(wrongPathTarget->application, wam::EvidenceSource::Executable,
                        wam::EvidenceStatus::Incomplete));

    for (const auto *target : {missingTarget, unavailableTarget, wrongPathTarget})
        QVERIFY(target->application.installState != wam::InstallState::PotentialOrphan);
}

QTEST_GUILESS_MAIN(ExecutableEvidenceTest)

#include "tst_executable_evidence.moc"
