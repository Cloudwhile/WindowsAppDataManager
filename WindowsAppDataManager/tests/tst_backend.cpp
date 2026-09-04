#include "src/core/classifier/DataClassifier.h"
#include "src/core/classifier/RiskAssessment.h"
#include "src/core/resolver/AppResolver.h"
#include "src/core/resolver/AttributionScorer.h"
#include "src/core/resolver/CandidateGenerator.h"
#include "src/core/resolver/InstallationResolver.h"
#include "src/core/rules/RuleCatalog.h"
#include "src/core/rules/GlobMatcher.h"
#include "src/core/rules/RuleLoader.h"
#include "src/core/scanner/DirectoryScanner.h"
#include "src/core/scanner/MetadataFingerprint.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <filesystem>

namespace {

void writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
}

std::filesystem::path fsPath(const QString &path)
{
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

QJsonObject validRuleObject(const QString &id = QStringLiteral("sample-app"))
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("version"), QStringLiteral("1")},
        {QStringLiteral("name"), QStringLiteral("Sample App")},
        {QStringLiteral("publisher"), QStringLiteral("Sample Publisher")},
        {QStringLiteral("applicationCategory"), QStringLiteral("工具")},
        {QStringLiteral("executablePath"), QStringLiteral("%LOCALAPPDATA%/Sample/app.exe")},
        {QStringLiteral("installPath"), QStringLiteral("%LOCALAPPDATA%/Sample")},
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
                 {QStringLiteral("impact"), QStringLiteral("Cache can be regenerated.")}
             }
         }}
    };
}

QByteArray ruleJson(const QJsonObject &rule)
{
    return QJsonDocument(rule).toJson(QJsonDocument::Compact);
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

} // namespace

class BackendTest final : public QObject {
    Q_OBJECT

private slots:
    void unknownRemainsUnknown();
    void safeRulesRequireExactPathEvidence();
    void sensitiveRulesTakePriority();
    void ruleLoaderAcceptsValidDocument();
    void ruleLoaderTracksRuleOriginAndTrust();
    void ruleLoaderAcceptsInstallationIdentifiers();
    void ruleLoaderRejectsInvalidDocuments();
    void ruleLoaderRejectsDuplicatePathAlternatives();
    void ruleLoaderRejectsInvalidInstallationIdentifiers();
    void ruleCatalogRejectsDuplicateApplicationIds();
    void ruleCatalogRejectsAmbiguousInstallationIdentifiers();
    void builtInCatalogDiscoversAllRuleDocuments();
    void applicationRulesUseBoundaryAndLongestMatch();
    void globRulesMatchControlledPatterns();
    void applicationRiskPreservesSixLevels();
    void attributionAndInstallationScorersKeepDomainsIndependent();
    void installationResolverDistinguishesNotObserved();
    void installationResolverConflictRemainsUnknown();
    void metadataFingerprintRemainsStable();
    void scannerHandlesUnicodeAndCountsFiles();
    void scannerCanSkipUnusedFingerprint();
    void scannerThrottlesActivityByFileCount();
    void scannerHonorsCancellationAndExclusions();
    void resolverProducesStableCollisionFreeIds();
    void resolverSeparatesChromeAndChromium();
    void resolverExcludesNestedKnownTargets();
    void resolverUsesExactInstallationEvidence();
    void resolverRejectsMismatchedInstallationEvidence();
    void resolverTreatsUnavailableEvidenceConservatively();
    void resolverDoesNotPromoteUnknownFoldersFromInstallationEvidence();
    void resolverPreservesUnknownCandidateEvidence();
    void candidateGeneratorRequiresIndependentInstallAnchor();
    void candidateGeneratorUsesAppxPackageIdentity();
    void candidateGeneratorUsesExecutableMetadata();
    void candidateGeneratorUsesExecutableParentDirectory();
    void candidateGeneratorUsesRunningProcessEvidenceInRanking();
    void candidateGeneratorMarksCloseCandidatesAmbiguous();
    void vendorNamespaceExpandsChildrenWithoutTreatingParentAsApplicationData();
    void resolverPublishesApplicationClassificationRules();
};

void BackendTest::unknownRemainsUnknown()
{
    const wam::core::Classification result =
            wam::core::DataClassifier().classify(fsPath(QStringLiteral("opaque/data.bin")));
    QCOMPARE(result.category, wam::DataCategory::Unknown);
    QCOMPARE(result.risk, wam::RiskLevel::Unknown);
    QCOMPARE(result.rebuildable, wam::RebuildableState::Unknown);
}

void BackendTest::safeRulesRequireExactPathEvidence()
{
    wam::core::DataClassifier classifier;
    QCOMPARE(classifier.classify(fsPath(QStringLiteral("Templates/resume.docx"))).risk,
             wam::RiskLevel::Unknown);
    QCOMPARE(classifier.classify(fsPath(QStringLiteral("catalog/data.bin"))).risk,
             wam::RiskLevel::Unknown);
    QCOMPARE(classifier.classify(fsPath(QStringLiteral("Cache/entry.bin"))).risk,
             wam::RiskLevel::Safe);
    QCOMPARE(classifier.classify(fsPath(QStringLiteral("output/app.log"))).risk,
             wam::RiskLevel::Low);
}

void BackendTest::sensitiveRulesTakePriority()
{
    wam::core::DataClassifier classifier;
    QCOMPARE(classifier.classify(fsPath(QStringLiteral("Cache/Login Data"))).risk,
             wam::RiskLevel::Protected);
    QCOMPARE(classifier.classify(fsPath(QStringLiteral("Cache/Session Storage/item"))).risk,
             wam::RiskLevel::High);
    QCOMPARE(classifier.classify(fsPath(QStringLiteral("Cache/state.sqlite"))).risk,
             wam::RiskLevel::High);
}

void BackendTest::ruleLoaderAcceptsValidDocument()
{
    const auto result = wam::core::rules::RuleLoader::load(
            ruleJson(validRuleObject()), QStringLiteral("测试规则"));
    QVERIFY(result.isValid());
    QVERIFY(result.rule.has_value());
    QCOMPARE(result.rule->id, QStringLiteral("sample-app"));
    QCOMPARE(result.rule->locations.size(), 1);
    QCOMPARE(result.rule->locations.constFirst().scope, wam::RuleScope::Local);
    QCOMPARE(result.rule->locations.constFirst().relativePath,
             QStringLiteral("Sample/App Data"));
    QCOMPARE(result.rule->entries.size(), 1);
    QCOMPARE(result.rule->entries.constFirst().category, wam::DataCategory::Cache);
    QCOMPARE(result.rule->entries.constFirst().risk, wam::RiskLevel::Safe);
    QCOMPARE(result.rule->entries.constFirst().rebuildable,
             wam::RebuildableState::Yes);
}

void BackendTest::ruleLoaderTracksRuleOriginAndTrust()
{
    const auto builtIn = wam::core::rules::RuleLoader::load(
            ruleJson(validRuleObject()),
            QStringLiteral(":/windowsappdatamanager/rules/builtin/sample.json"));
    QVERIFY(builtIn.rule.has_value());
    QCOMPARE(builtIn.rule->origin, wam::RuleOrigin::BuiltIn);
    QCOMPARE(builtIn.rule->trustLevel, wam::RuleTrustLevel::Verified);

    const auto community = wam::core::rules::RuleLoader::load(
            ruleJson(validRuleObject(QStringLiteral("community-app"))),
            QStringLiteral("community/sample.json"));
    QVERIFY(community.rule.has_value());
    QCOMPARE(community.rule->origin, wam::RuleOrigin::Community);
    QCOMPARE(community.rule->trustLevel, wam::RuleTrustLevel::Unverified);

    const auto local = wam::core::rules::RuleLoader::load(
            ruleJson(validRuleObject(QStringLiteral("local-app"))),
            QStringLiteral("C:/Users/test/AppData/Local/rules/local.json"));
    QVERIFY(local.rule.has_value());
    QCOMPARE(local.rule->origin, wam::RuleOrigin::Local);
    QCOMPARE(local.rule->trustLevel, wam::RuleTrustLevel::Unverified);

    const auto memory = wam::core::rules::RuleLoader::load(
            ruleJson(validRuleObject(QStringLiteral("memory-app"))), {});
    QVERIFY(memory.rule.has_value());
    QCOMPARE(memory.rule->origin, wam::RuleOrigin::Local);
    QCOMPARE(memory.rule->trustLevel, wam::RuleTrustLevel::Unverified);
}

void BackendTest::ruleLoaderAcceptsInstallationIdentifiers()
{
    QJsonObject document = validRuleObject();
    document.insert(QStringLiteral("identifiers"), QJsonObject {
        {QStringLiteral("registryDisplayNames"), QJsonArray {
             QStringLiteral("  Sample App  "), QStringLiteral("Sample App Preview")
         }},
        {QStringLiteral("registryPublishers"), QJsonArray {
             QStringLiteral("  Sample Publisher  ")
         }},
        {QStringLiteral("appxPackageNames"), QJsonArray {
             QStringLiteral("Sample.App")
         }},
        {QStringLiteral("appxPublishers"), QJsonArray {
             QStringLiteral("CN=Sample Publisher")
         }},
        {QStringLiteral("runningProcessNames"), QJsonArray {
             QStringLiteral("Sample.exe"), QStringLiteral("Sample.Helper.EXE")
         }}
    });

    const auto result = wam::core::rules::RuleLoader::load(
            ruleJson(document), QStringLiteral("identifiers.json"));
    QVERIFY(result.isValid());
    QVERIFY(result.rule.has_value());
    QCOMPARE(result.rule->identifiers.registryDisplayNames,
             QStringList({QStringLiteral("Sample App"),
                          QStringLiteral("Sample App Preview")}));
    QCOMPARE(result.rule->identifiers.registryPublishers,
             QStringList({QStringLiteral("Sample Publisher")}));
    QCOMPARE(result.rule->identifiers.appxPackageNames,
             QStringList({QStringLiteral("Sample.App")}));
    QCOMPARE(result.rule->identifiers.appxPublishers,
             QStringList({QStringLiteral("CN=Sample Publisher")}));
    QCOMPARE(result.rule->identifiers.runningProcessNames,
             QStringList({QStringLiteral("Sample.exe"),
                          QStringLiteral("Sample.Helper.EXE")}));
}

void BackendTest::ruleLoaderRejectsInvalidDocuments()
{
    QJsonObject missingField = validRuleObject();
    missingField.remove(QStringLiteral("publisher"));
    const auto missingResult = wam::core::rules::RuleLoader::load(
            ruleJson(missingField), QStringLiteral("missing.json"));
    QVERIFY(!missingResult.rule.has_value());
    QVERIFY(hasRuleIssue(missingResult.issues, wam::RuleIssueCode::MissingField,
                         QStringLiteral("publisher")));

    QJsonObject unknownField = validRuleObject();
    unknownField.insert(QStringLiteral("cleanupByDefault"), true);
    const auto unknownFieldResult = wam::core::rules::RuleLoader::load(
            ruleJson(unknownField), QStringLiteral("unknown-field.json"));
    QVERIFY(!unknownFieldResult.rule.has_value());
    QVERIFY(hasRuleIssue(unknownFieldResult.issues, wam::RuleIssueCode::InvalidValue,
                         QStringLiteral("cleanupByDefault")));

    QJsonObject unknownEnum = validRuleObject();
    QJsonArray unknownEntries = unknownEnum.value(QStringLiteral("entries")).toArray();
    QJsonObject unknownEntry = unknownEntries.at(0).toObject();
    unknownEntry.insert(QStringLiteral("risk"), QStringLiteral("probably-safe"));
    unknownEntry.insert(QStringLiteral("category"), QStringLiteral("mystery"));
    unknownEntries.replace(0, unknownEntry);
    unknownEnum.insert(QStringLiteral("entries"), unknownEntries);
    const auto enumResult = wam::core::rules::RuleLoader::load(
            ruleJson(unknownEnum), QStringLiteral("enum.json"));
    QVERIFY(!enumResult.rule.has_value());
    QVERIFY(hasRuleIssue(enumResult.issues, wam::RuleIssueCode::InvalidValue,
                         QStringLiteral("category")));
    QVERIFY(hasRuleIssue(enumResult.issues, wam::RuleIssueCode::InvalidValue,
                         QStringLiteral("risk")));

    QJsonObject absolutePath = validRuleObject();
    QJsonArray absoluteLocations = absolutePath.value(QStringLiteral("locations")).toArray();
    QJsonObject absoluteLocation = absoluteLocations.at(0).toObject();
    absoluteLocation.insert(QStringLiteral("path"), QStringLiteral("C:/Users/Public/Data"));
    absoluteLocations.replace(0, absoluteLocation);
    absolutePath.insert(QStringLiteral("locations"), absoluteLocations);
    const auto absoluteResult = wam::core::rules::RuleLoader::load(
            ruleJson(absolutePath), QStringLiteral("absolute.json"));
    QVERIFY(!absoluteResult.rule.has_value());
    QVERIFY(hasRuleIssue(absoluteResult.issues, wam::RuleIssueCode::UnsafePath,
                         QStringLiteral("locations[0].path")));

    QJsonObject parentPath = validRuleObject();
    QJsonArray parentEntries = parentPath.value(QStringLiteral("entries")).toArray();
    QJsonObject parentEntry = parentEntries.at(0).toObject();
    parentEntry.insert(QStringLiteral("path"), QStringLiteral("../Cache"));
    parentEntries.replace(0, parentEntry);
    parentPath.insert(QStringLiteral("entries"), parentEntries);
    const auto parentResult = wam::core::rules::RuleLoader::load(
            ruleJson(parentPath), QStringLiteral("parent.json"));
    QVERIFY(!parentResult.rule.has_value());
    QVERIFY(hasRuleIssue(parentResult.issues, wam::RuleIssueCode::UnsafePath,
                         QStringLiteral("entries[0].path")));
}

void BackendTest::ruleLoaderRejectsDuplicatePathAlternatives()
{
    QJsonObject executableDuplicates = validRuleObject();
    executableDuplicates.insert(QStringLiteral("executables"), QJsonArray {
        QStringLiteral("%LOCALAPPDATA%/Sample/app.exe"),
        QStringLiteral("%LOCALAPPDATA%\\Sample\\APP.EXE")
    });
    const auto executableResult = wam::core::rules::RuleLoader::load(
            ruleJson(executableDuplicates), QStringLiteral("duplicate-executables.json"));
    QVERIFY(!executableResult.rule.has_value());
    QVERIFY(hasRuleIssue(executableResult.issues, wam::RuleIssueCode::InvalidValue,
                         QStringLiteral("executables[1]")));

    QJsonObject installDuplicates = validRuleObject();
    installDuplicates.insert(QStringLiteral("installPaths"), QJsonArray {
        QStringLiteral("%LOCALAPPDATA%/Sample"),
        QStringLiteral("%LOCALAPPDATA%\\SAMPLE")
    });
    const auto installResult = wam::core::rules::RuleLoader::load(
            ruleJson(installDuplicates), QStringLiteral("duplicate-install-paths.json"));
    QVERIFY(!installResult.rule.has_value());
    QVERIFY(hasRuleIssue(installResult.issues, wam::RuleIssueCode::InvalidValue,
                         QStringLiteral("installPaths[1]")));
}

void BackendTest::ruleLoaderRejectsInvalidInstallationIdentifiers()
{
    QJsonObject wrongType = validRuleObject();
    wrongType.insert(QStringLiteral("identifiers"), QStringLiteral("Sample App"));
    const auto wrongTypeResult = wam::core::rules::RuleLoader::load(
            ruleJson(wrongType), QStringLiteral("identifier-type.json"));
    QVERIFY(!wrongTypeResult.rule.has_value());
    QVERIFY(hasRuleIssue(wrongTypeResult.issues, wam::RuleIssueCode::InvalidType,
                         QStringLiteral("identifiers")));

    QJsonObject unknownField = validRuleObject();
    unknownField.insert(QStringLiteral("identifiers"), QJsonObject {
        {QStringLiteral("processNames"), QJsonArray {QStringLiteral("sample.exe")}}
    });
    const auto unknownFieldResult = wam::core::rules::RuleLoader::load(
            ruleJson(unknownField), QStringLiteral("identifier-unknown-field.json"));
    QVERIFY(!unknownFieldResult.rule.has_value());
    QVERIFY(hasRuleIssue(unknownFieldResult.issues, wam::RuleIssueCode::InvalidValue,
                         QStringLiteral("identifiers.processNames")));

    QJsonObject emptyArray = validRuleObject();
    emptyArray.insert(QStringLiteral("identifiers"), QJsonObject {
        {QStringLiteral("registryDisplayNames"), QJsonArray {}}
    });
    const auto emptyArrayResult = wam::core::rules::RuleLoader::load(
            ruleJson(emptyArray), QStringLiteral("identifier-empty-array.json"));
    QVERIFY(!emptyArrayResult.rule.has_value());
    QVERIFY(hasRuleIssue(emptyArrayResult.issues, wam::RuleIssueCode::InvalidValue,
                         QStringLiteral("identifiers.registryDisplayNames")));

    QJsonObject emptyItem = validRuleObject();
    emptyItem.insert(QStringLiteral("identifiers"), QJsonObject {
        {QStringLiteral("registryPublishers"), QJsonArray {QStringLiteral(" \t ")}}
    });
    const auto emptyItemResult = wam::core::rules::RuleLoader::load(
            ruleJson(emptyItem), QStringLiteral("identifier-empty-item.json"));
    QVERIFY(!emptyItemResult.rule.has_value());
    QVERIFY(hasRuleIssue(emptyItemResult.issues, wam::RuleIssueCode::InvalidValue,
                         QStringLiteral("identifiers.registryPublishers[0]")));

    QJsonObject wrongItemType = validRuleObject();
    wrongItemType.insert(QStringLiteral("identifiers"), QJsonObject {
        {QStringLiteral("appxPackageNames"), QJsonArray {42}}
    });
    const auto wrongItemTypeResult = wam::core::rules::RuleLoader::load(
            ruleJson(wrongItemType), QStringLiteral("identifier-item-type.json"));
    QVERIFY(!wrongItemTypeResult.rule.has_value());
    QVERIFY(hasRuleIssue(wrongItemTypeResult.issues, wam::RuleIssueCode::InvalidType,
                         QStringLiteral("identifiers.appxPackageNames[0]")));

    QJsonObject duplicateItem = validRuleObject();
    duplicateItem.insert(QStringLiteral("identifiers"), QJsonObject {
        {QStringLiteral("appxPublishers"), QJsonArray {
             QStringLiteral("CN=Sample Publisher"),
             QStringLiteral("  cn=sample publisher  ")
         }}
    });
    const auto duplicateItemResult = wam::core::rules::RuleLoader::load(
            ruleJson(duplicateItem), QStringLiteral("identifier-duplicate.json"));
    QVERIFY(!duplicateItemResult.rule.has_value());
    QVERIFY(hasRuleIssue(duplicateItemResult.issues, wam::RuleIssueCode::InvalidValue,
                         QStringLiteral("identifiers.appxPublishers[1]")));

    QJsonObject publisherOnly = validRuleObject();
    publisherOnly.insert(QStringLiteral("identifiers"), QJsonObject {
        {QStringLiteral("registryPublishers"), QJsonArray {
             QStringLiteral("Sample Publisher")
         }}
    });
    const auto publisherOnlyResult = wam::core::rules::RuleLoader::load(
            ruleJson(publisherOnly), QStringLiteral("identifier-publisher-only.json"));
    QVERIFY(!publisherOnlyResult.rule.has_value());
    QVERIFY(hasRuleIssue(publisherOnlyResult.issues, wam::RuleIssueCode::InvalidValue,
                         QStringLiteral("identifiers.registryPublishers")));
}

void BackendTest::ruleCatalogRejectsDuplicateApplicationIds()
{
    const QByteArray document = ruleJson(validRuleObject());
    const auto catalog = wam::core::rules::RuleCatalog::fromJsonDocuments({
        {QStringLiteral("first.json"), document},
        {QStringLiteral("second.json"), document}
    });
    QCOMPARE(catalog.applications().size(), 1);
    QVERIFY(hasRuleIssue(catalog.issues(), wam::RuleIssueCode::DuplicateId,
                         QStringLiteral("id")));
}

void BackendTest::ruleCatalogRejectsAmbiguousInstallationIdentifiers()
{
    QJsonObject first = validRuleObject(QStringLiteral("first-app"));
    first.insert(QStringLiteral("identifiers"), QJsonObject {
        {QStringLiteral("registryDisplayNames"), QJsonArray {QStringLiteral("Shared App")}},
        {QStringLiteral("registryPublishers"), QJsonArray {QStringLiteral("Publisher A")}}
    });
    QJsonObject conflicting = validRuleObject(QStringLiteral("conflicting-app"));
    conflicting.insert(QStringLiteral("identifiers"), QJsonObject {
        {QStringLiteral("registryDisplayNames"), QJsonArray {QStringLiteral(" shared app ")}},
        {QStringLiteral("registryPublishers"), QJsonArray {QStringLiteral("publisher a")}}
    });

    const auto conflictCatalog = wam::core::rules::RuleCatalog::fromJsonDocuments({
        {QStringLiteral("first.json"), ruleJson(first)},
        {QStringLiteral("conflicting.json"), ruleJson(conflicting)}
    });
    QCOMPARE(conflictCatalog.applications().size(), 1);
    QVERIFY(hasRuleIssue(conflictCatalog.issues(),
                         wam::RuleIssueCode::AmbiguousIdentifier,
                         QStringLiteral("identifiers.registryDisplayNames")));

    QJsonObject distinctPublisher = validRuleObject(QStringLiteral("distinct-app"));
    distinctPublisher.insert(QStringLiteral("executablePath"),
                             QStringLiteral("C:/Program Files/Distinct/distinct.exe"));
    distinctPublisher.insert(QStringLiteral("installPath"),
                             QStringLiteral("C:/Program Files/Distinct"));
    distinctPublisher.insert(QStringLiteral("identifiers"), QJsonObject {
        {QStringLiteral("registryDisplayNames"), QJsonArray {QStringLiteral("Shared App")}},
        {QStringLiteral("registryPublishers"), QJsonArray {QStringLiteral("Publisher B")}}
    });
    const auto distinctCatalog = wam::core::rules::RuleCatalog::fromJsonDocuments({
        {QStringLiteral("first.json"), ruleJson(first)},
        {QStringLiteral("distinct.json"), ruleJson(distinctPublisher)}
    });
    QCOMPARE(distinctCatalog.applications().size(), 2);
    QVERIFY(distinctCatalog.issues().isEmpty());
}

void BackendTest::builtInCatalogDiscoversAllRuleDocuments()
{
    const auto &catalog = wam::core::rules::RuleCatalog::builtIn();
    QVERIFY2(catalog.issues().isEmpty(), "内置规则必须全部通过加载校验");
    QVERIFY(catalog.findById(QStringLiteral("google-chrome")));
    QVERIFY(catalog.findById(QStringLiteral("chromium")));
    QVERIFY(catalog.findById(QStringLiteral("microsoft-edge")));
    QVERIFY(catalog.findById(QStringLiteral("brave-browser")));
    QVERIFY(catalog.findById(QStringLiteral("discord")));
    QVERIFY(catalog.findById(QStringLiteral("visual-studio-code")));
    QVERIFY(catalog.findById(QStringLiteral("jetbrains")));
    QVERIFY(catalog.findById(QStringLiteral("windows-crash-dumps")));
    QVERIFY(catalog.findById(QStringLiteral("npm-cache")));
}

void BackendTest::applicationRulesUseBoundaryAndLongestMatch()
{
    const QVector<wam::RuleEntry> entries {
        {QStringLiteral("assets"), QStringLiteral("Assets"),
         wam::DataCategory::Config, wam::RiskLevel::Caution,
         wam::RebuildableState::No, QStringLiteral("保留资源状态。")},
        {QStringLiteral("generated-assets"), QStringLiteral("assets\\Generated"),
         wam::DataCategory::Cache, wam::RiskLevel::Safe,
         wam::RebuildableState::Yes, QStringLiteral("资源可重新生成。")},
        {QStringLiteral("cache"), QStringLiteral("Cache"),
         wam::DataCategory::Cache, wam::RiskLevel::Safe,
         wam::RebuildableState::Yes, QStringLiteral("缓存可重新生成。")}
    };
    const QString source = QStringLiteral("内置规则 / sample-app@1");
    const wam::core::DataClassifier classifier;
    const wam::core::DataClassifier preparedClassifier(entries, source);

    const auto longest = classifier.classify(
            fsPath(QStringLiteral("ASSETS/GENERATED/item.bin")), entries, source);
    QCOMPARE(longest.id, QStringLiteral("generated-assets"));
    QCOMPARE(longest.risk, wam::RiskLevel::Safe);
    QCOMPARE(longest.ruleSource, source);
    const auto preparedLongest = preparedClassifier.classify(
            fsPath(QStringLiteral("ASSETS/GENERATED/item.bin")));
    QCOMPARE(preparedLongest.id, longest.id);
    QCOMPARE(preparedLongest.risk, longest.risk);
    QCOMPARE(preparedLongest.rebuildable, longest.rebuildable);
    QCOMPARE(preparedLongest.ruleSource, longest.ruleSource);
    QCOMPARE(preparedLongest.matchedPath,
             QStringLiteral("assets\\Generated"));

    const auto base = classifier.classify(
            fsPath(QStringLiteral("assets/raw.bin")), entries, source);
    QCOMPARE(base.id, QStringLiteral("assets"));
    QCOMPARE(base.risk, wam::RiskLevel::Caution);

    const auto boundary = classifier.classify(
            fsPath(QStringLiteral("AssetsGenerated/item.bin")), entries, source);
    QCOMPARE(boundary.risk, wam::RiskLevel::Unknown);

    const auto protectedResult = classifier.classify(
            fsPath(QStringLiteral("Cache/Login Data")), entries, source);
    QCOMPARE(protectedResult.risk, wam::RiskLevel::Protected);
    QVERIFY(protectedResult.ruleSource.startsWith(QStringLiteral("启发式")));
}

void BackendTest::globRulesMatchControlledPatterns()
{
    QVERIFY(wam::core::rules::GlobMatcher::validate(
            QStringLiteral("Profile */Cache/**")));
    QVERIFY(wam::core::rules::GlobMatcher::matches(
            QStringLiteral("Profile */Cache/**"),
            QStringLiteral("Profile 2/Cache/data.bin")));
    QVERIFY(wam::core::rules::GlobMatcher::matches(
            QStringLiteral("*/caches/**"),
            QStringLiteral("IntelliJIdea2026.1/caches/index/file")));
    QVERIFY(!wam::core::rules::GlobMatcher::matches(
            QStringLiteral("Profile */Cache/**"),
            QStringLiteral("Profile 2/Config/data.bin")));
    QString error;
    QVERIFY(!wam::core::rules::GlobMatcher::validate(
            QStringLiteral("../Cache/**"), &error));
    QVERIFY(!wam::core::rules::GlobMatcher::validate(
            QStringLiteral("Cache/**/../Secrets"), &error));
}

void BackendTest::applicationRiskPreservesSixLevels()
{
    wam::ApplicationInfo application;
    application.confidence = 90;
    application.dataGroups = {{.risk = wam::RiskLevel::Safe}};
    QCOMPARE(wam::core::applicationRisk(application), wam::RiskLevel::Safe);

    application.dataGroups.append({.risk = wam::RiskLevel::Unknown});
    QCOMPARE(wam::core::applicationRisk(application), wam::RiskLevel::Unknown);
    application.dataGroups.append({.risk = wam::RiskLevel::High});
    QCOMPARE(wam::core::applicationRisk(application), wam::RiskLevel::High);
    application.dataGroups.append({.risk = wam::RiskLevel::Protected});
    QCOMPARE(wam::core::applicationRisk(application), wam::RiskLevel::Protected);

    application.confidence = 20;
    QCOMPARE(wam::core::applicationRisk(application), wam::RiskLevel::Unknown);

    application.attribution = {
        wam::AttributionState::Verified,
        90,
        {{wam::EvidenceSource::Rule, wam::EvidenceStatus::Matched,
          QStringLiteral("精确规则匹配")}}
    };
    application.dataGroups = {{.risk = wam::RiskLevel::Safe}};
    QCOMPARE(wam::core::applicationRisk(application), wam::RiskLevel::Safe);

    application.confidence = 99;
    application.attribution.confidence = 20;
    QCOMPARE(wam::core::applicationRisk(application), wam::RiskLevel::Unknown);
}

void BackendTest::attributionAndInstallationScorersKeepDomainsIndependent()
{
    const auto exact = wam::core::AttributionScorer::evaluate({
        .exactRule = true,
        .conflict = true
    });
    QCOMPARE(exact.state, wam::AttributionState::Verified);
    QCOMPARE(exact.confidence, 100);
    QCOMPARE(exact.compatibilityConfidence, 49);

    const auto inferred = wam::core::AttributionScorer::evaluate({
        .exactRule = false,
        .registryMatched = true,
        .appxMatched = true
    });
    QCOMPARE(inferred.state, wam::AttributionState::Unknown);
    QCOMPARE(inferred.confidence, 0);
    QCOMPARE(inferred.compatibilityConfidence, 98);

    const auto notObserved = wam::core::InstallationResolver::evaluate({});
    QCOMPARE(notObserved.state, wam::InstallationState::Unknown);
    QCOMPARE(notObserved.confidence, 0);

    const auto installed = wam::core::InstallationResolver::evaluate({2});
    QCOMPARE(installed.state, wam::InstallationState::Installed);
    QCOMPARE(installed.confidence, 90);
}

void BackendTest::installationResolverDistinguishesNotObserved()
{
    const auto notObserved = wam::core::InstallationResolver::evaluate({
        .negativeEvidenceCount = 3,
        .requiredNegativeEvidenceCount = 3,
        .evidenceComplete = true
    });
    QCOMPARE(notObserved.state, wam::InstallationState::NotObserved);
    QCOMPARE(notObserved.confidence, 90);

    const auto incomplete = wam::core::InstallationResolver::evaluate({
        .negativeEvidenceCount = 3,
        .requiredNegativeEvidenceCount = 3,
        .evidenceComplete = false
    });
    QCOMPARE(incomplete.state, wam::InstallationState::Unknown);
}

void BackendTest::installationResolverConflictRemainsUnknown()
{
    const auto conflicted = wam::core::InstallationResolver::evaluate({
        .negativeEvidenceCount = 3,
        .requiredNegativeEvidenceCount = 3,
        .evidenceComplete = true,
        .conflict = true
    });
    QCOMPARE(conflicted.state, wam::InstallationState::Unknown);
    QCOMPARE(conflicted.confidence, 0);
}

void BackendTest::metadataFingerprintRemainsStable()
{
    wam::core::MetadataFingerprint fingerprint;
    fingerprint.add(QStringLiteral("cache/entry.bin"), 7, 1700000000123);
    fingerprint.add(QStringLiteral("logs/trace.log"), 0, 1699999999000);
    QCOMPARE(fingerprint.value(),
             QStringLiteral("3a85cacfcaeb0b8d2b2059643e9eb63ac9ab17964a08045e9ab8b896f2c08c7a"));
}

void BackendTest::scannerHandlesUnicodeAndCountsFiles()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString unicodeDirectory = QDir(temporary.path()).filePath(QStringLiteral("应用数据/缓存"));
    QVERIFY(QDir().mkpath(unicodeDirectory));
    writeFile(QDir(unicodeDirectory).filePath(QStringLiteral("文件一.bin")), QByteArray(7, 'a'));
    writeFile(QDir(temporary.path()).filePath(QStringLiteral("root.bin")), QByteArray(5, 'b'));

    std::atomic_bool cancelled = false;
    QStringList visited;
    const auto stats = wam::core::DirectoryScanner().scan(
            temporary.path(), cancelled,
            [&visited](const auto &path, quint64, qint64) {
#ifdef _WIN32
                visited.append(QString::fromStdWString(path.generic_wstring()));
#else
                visited.append(QString::fromStdString(path.generic_string()));
#endif
            }, {});

    QCOMPARE(stats.fileCount, quint64(2));
    QCOMPARE(stats.totalSize, quint64(12));
    QVERIFY(std::any_of(visited.cbegin(), visited.cend(), [](const QString &path) {
        return path.contains(QStringLiteral("文件一.bin"));
    }));
}

void BackendTest::scannerCanSkipUnusedFingerprint()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    writeFile(QDir(temporary.path()).filePath(QStringLiteral("first.bin")),
              QByteArray(7, 'a'));
    writeFile(QDir(temporary.path()).filePath(QStringLiteral("second.bin")),
              QByteArray(5, 'b'));

    std::atomic_bool cancelled = false;
    quint64 defaultVisited = 0;
    QStringList reportedPaths;
    const auto defaultStats = wam::core::DirectoryScanner().scan(
            temporary.path(), cancelled,
            [&defaultVisited](const auto &, quint64, qint64) {
                ++defaultVisited;
            }, [&reportedPaths](const QString &path, quint64) {
                reportedPaths.append(path);
            });

    quint64 optimizedVisited = 0;
    const auto optimizedStats = wam::core::DirectoryScanner().scan(
            temporary.path(), cancelled,
            [&optimizedVisited](const auto &, quint64, qint64) {
                ++optimizedVisited;
            }, {}, {}, false, false);

    QCOMPARE(optimizedStats.fileCount, defaultStats.fileCount);
    QCOMPARE(optimizedStats.totalSize, defaultStats.totalSize);
    QCOMPARE(optimizedStats.latestModifiedMilliseconds,
             defaultStats.latestModifiedMilliseconds);
    QCOMPARE(optimizedVisited, defaultVisited);
    QVERIFY(!reportedPaths.isEmpty());
    QVERIFY(QFileInfo(reportedPaths.constFirst()).isFile());
    QVERIFY(!defaultStats.metadataFingerprint.isEmpty());
    QVERIFY(optimizedStats.metadataFingerprint.isEmpty());

    const auto verifiedStats = wam::core::DirectoryScanner().scan(
            temporary.path(), cancelled, {}, {}, {}, true, false);
    QVERIFY(verifiedStats.stabilityVerified);
    QVERIFY(!verifiedStats.metadataFingerprint.isEmpty());
}

void BackendTest::scannerThrottlesActivityByFileCount()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    for (int index = 0; index < 257; ++index) {
        writeFile(QDir(temporary.path()).filePath(
                          QStringLiteral("entry-%1.bin").arg(index, 3, 10, QLatin1Char('0'))),
                  {});
    }

    std::atomic_bool cancelled = false;
    QVector<quint64> reportedCounts;
    const auto stats = wam::core::DirectoryScanner().scan(
            temporary.path(), cancelled, {},
            [&reportedCounts](const QString &, quint64 filesVisited) {
                reportedCounts.append(filesVisited);
            });

    QCOMPARE(stats.fileCount, quint64(257));
    QCOMPARE(reportedCounts, QVector<quint64>({1, 256}));
}

void BackendTest::scannerHonorsCancellationAndExclusions()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString excluded = QDir(temporary.path()).filePath(QStringLiteral("Known/Data"));
    QVERIFY(QDir().mkpath(excluded));
    writeFile(QDir(excluded).filePath(QStringLiteral("excluded.bin")), QByteArray(10, 'x'));
    writeFile(QDir(temporary.path()).filePath(QStringLiteral("included.bin")), QByteArray(3, 'y'));

    std::atomic_bool cancelled = false;
    QString exclusion = excluded;
#ifdef Q_OS_WIN
    exclusion = QDir::fromNativeSeparators(
            QDir(excluded).filePath(QStringLiteral("../Data"))).toUpper();
#endif
    const auto stats = wam::core::DirectoryScanner().scan(
            temporary.path(), cancelled, {}, {}, {exclusion});
    QCOMPARE(stats.fileCount, quint64(1));
    QCOMPARE(stats.totalSize, quint64(3));

    cancelled.store(true);
    const auto cancelledStats = wam::core::DirectoryScanner().scan(
            temporary.path(), cancelled, {}, {});
    QVERIFY(cancelledStats.cancelled);
    QCOMPARE(cancelledStats.fileCount, quint64(0));

    QTemporaryDir cancellationTemporary;
    QVERIFY(cancellationTemporary.isValid());
    writeFile(QDir(cancellationTemporary.path()).filePath(
                      QStringLiteral("only-file.bin")), QByteArray(4, 'z'));

    cancelled.store(false);
    quint64 visited = 0;
    const auto interruptedStats = wam::core::DirectoryScanner().scan(
            cancellationTemporary.path(), cancelled,
            [&cancelled, &visited](const auto &, quint64, qint64) {
                ++visited;
                cancelled.store(true, std::memory_order_relaxed);
            }, {});
    QVERIFY(interruptedStats.cancelled);
    QCOMPARE(interruptedStats.fileCount, visited);
    QCOMPARE(visited, quint64(1));
}

void BackendTest::resolverProducesStableCollisionFreeIds()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QVERIFY(QDir().mkpath(QDir(temporary.path()).filePath(QStringLiteral("微信"))));
    QVERIFY(QDir().mkpath(QDir(temporary.path()).filePath(QStringLiteral("网易"))));

    const auto first = wam::core::AppResolver().discoverTargets({temporary.path()});
    const auto second = wam::core::AppResolver().discoverTargets({temporary.path()});
    QCOMPARE(first.size(), 2);
    QCOMPARE(second.size(), 2);
    QVERIFY(first[0].application.id != first[1].application.id);
    QCOMPARE(first[0].application.id, second[0].application.id);
    QCOMPARE(first[1].application.id, second[1].application.id);
}

void BackendTest::resolverSeparatesChromeAndChromium()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    QVERIFY(QDir().mkpath(QDir(local).filePath(
            QStringLiteral("Google/Chrome/User Data/Default/Cache"))));
    QVERIFY(QDir().mkpath(QDir(local).filePath(
            QStringLiteral("Chromium/User Data/Default/Cache"))));

    const auto targets = wam::core::AppResolver().discoverTargets({local});
    const auto chrome = std::find_if(targets.cbegin(), targets.cend(), [](const auto &target) {
        return target.application.id == QStringLiteral("google-chrome");
    });
    const auto chromium = std::find_if(targets.cbegin(), targets.cend(), [](const auto &target) {
        return target.application.id == QStringLiteral("chromium");
    });

    QVERIFY(chrome != targets.cend());
    QVERIFY(chromium != targets.cend());
    QVERIFY(chrome->path != chromium->path);
    QVERIFY(!chrome->classificationRules.isEmpty());
    QVERIFY(!chromium->classificationRules.isEmpty());
    QVERIFY(chrome->ruleSource.contains(QStringLiteral("google-chrome@1")));
    QVERIFY(chromium->ruleSource.contains(QStringLiteral("chromium@1")));
    QVERIFY(chrome->application.confidence >= 70);
    QVERIFY(chromium->application.confidence >= 70);
}

void BackendTest::resolverExcludesNestedKnownTargets()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    const QString chrome = QDir(local).filePath(QStringLiteral("Google/Chrome/User Data"));
    const QString drive = QDir(local).filePath(QStringLiteral("Google/DriveFS"));
    QVERIFY(QDir().mkpath(chrome));
    QVERIFY(QDir().mkpath(drive));

    const auto targets = wam::core::AppResolver().discoverTargets({local});
    const auto google = std::find_if(targets.cbegin(), targets.cend(), [](const auto &target) {
        return target.application.name.compare(QStringLiteral("Google"), Qt::CaseInsensitive) == 0;
    });
    QVERIFY(google != targets.cend());
    QVERIFY(!google->excludedPaths.isEmpty());
    QVERIFY(std::any_of(google->excludedPaths.cbegin(), google->excludedPaths.cend(),
                        [](const QString &path) {
        return path.contains(QStringLiteral("chrome"), Qt::CaseInsensitive);
    }));
}

void BackendTest::resolverUsesExactInstallationEvidence()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    QVERIFY(QDir().mkpath(QDir(local).filePath(QStringLiteral("Sample/App Data"))));

    QJsonObject rule = validRuleObject();
    rule.insert(QStringLiteral("executablePath"),
                QStringLiteral("%LOCALAPPDATA%/WAM-Evidence-Test-Missing/sample.exe"));
    rule.insert(QStringLiteral("identifiers"), QJsonObject {
        {QStringLiteral("registryDisplayNames"), QJsonArray {QStringLiteral("Sample App")}},
        {QStringLiteral("registryPublishers"), QJsonArray {QStringLiteral("Sample Publisher")}},
        {QStringLiteral("appxPackageNames"), QJsonArray {QStringLiteral("Sample.App")}},
        {QStringLiteral("appxPublishers"), QJsonArray {QStringLiteral("CN=Sample Publisher")}}
    });
    const auto catalog = wam::core::rules::RuleCatalog::fromJsonDocuments({
        {QStringLiteral("sample.json"), ruleJson(rule)}
    });
    QVERIFY(catalog.issues().isEmpty());

    wam::InstallationEvidenceSnapshot evidence;
    evidence.registry.availability = wam::InstallationEvidenceAvailability::Complete;
    evidence.appx.availability = wam::InstallationEvidenceAvailability::Complete;
    evidence.registry.records.append({
        .identity = QStringLiteral("HKCU/sample"),
        .displayName = QStringLiteral("  sample app  "),
        .publisher = QStringLiteral("SAMPLE PUBLISHER"),
        .installPath = QStringLiteral("C:/Apps/Sample")
    });
    evidence.appx.records.append({
        .packageName = QStringLiteral("sample.app"),
        .publisher = QStringLiteral("cn=sample publisher"),
        .packageFamilyName = QStringLiteral("Sample.App_family"),
        .displayName = QStringLiteral("Sample App"),
        .installPath = QStringLiteral("C:/Program Files/WindowsApps/Sample.App")
    });

    const auto targets = wam::core::AppResolver(catalog, evidence).discoverTargets({local});
    const auto sample = std::find_if(targets.cbegin(), targets.cend(), [](const auto &target) {
        return target.application.id == QStringLiteral("sample-app");
    });
    QVERIFY(sample != targets.cend());
    QCOMPARE(sample->application.installState, wam::InstallState::Installed);
    QCOMPARE(sample->application.confidence, 98);
    QCOMPARE(sample->application.attribution.state,
             wam::AttributionState::Verified);
    QCOMPARE(sample->application.attribution.confidence, 100);
    QCOMPARE(sample->application.installation.state,
             wam::InstallationState::Installed);
    QCOMPARE(sample->application.installation.confidence, 90);
    QCOMPARE(sample->application.attribution.evidence.size(), 2);
    QVERIFY(std::none_of(sample->application.installation.evidence.cbegin(),
                         sample->application.installation.evidence.cend(),
                         [](const auto &item) {
        return item.source == wam::EvidenceSource::Folder
                || item.source == wam::EvidenceSource::Rule;
    }));
    QCOMPARE(sample->application.installPath, QDir::toNativeSeparators(
                     QStringLiteral("C:/Apps/Sample")));
    QVERIFY(std::any_of(sample->application.evidence.cbegin(),
                        sample->application.evidence.cend(), [](const auto &item) {
        return item.source == wam::EvidenceSource::Registry
                && item.status == wam::EvidenceStatus::Matched;
    }));
    QVERIFY(std::any_of(sample->application.evidence.cbegin(),
                        sample->application.evidence.cend(), [](const auto &item) {
        return item.source == wam::EvidenceSource::Appx
                && item.status == wam::EvidenceStatus::Matched;
    }));
    QCOMPARE(std::count_if(sample->application.evidence.cbegin(),
                           sample->application.evidence.cend(), [](const auto &item) {
        return item.source == wam::EvidenceSource::Publisher
                && item.status == wam::EvidenceStatus::Matched;
    }), 2);
}

void BackendTest::resolverRejectsMismatchedInstallationEvidence()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    QVERIFY(QDir().mkpath(QDir(local).filePath(QStringLiteral("Sample/App Data"))));

    QJsonObject rule = validRuleObject();
    rule.insert(QStringLiteral("executablePath"),
                QStringLiteral("%LOCALAPPDATA%/WAM-Evidence-Test-Missing/sample.exe"));
    rule.insert(QStringLiteral("identifiers"), QJsonObject {
        {QStringLiteral("registryDisplayNames"), QJsonArray {QStringLiteral("Sample App")}},
        {QStringLiteral("registryPublishers"), QJsonArray {QStringLiteral("Sample Publisher")}}
    });
    const auto catalog = wam::core::rules::RuleCatalog::fromJsonDocuments({
        {QStringLiteral("sample.json"), ruleJson(rule)}
    });

    wam::InstallationEvidenceSnapshot evidence;
    evidence.registry.availability = wam::InstallationEvidenceAvailability::Complete;
    evidence.registry.records.append({
        .displayName = QStringLiteral("Sample App"),
        .publisher = QStringLiteral("Different Publisher")
    });

    const auto targets = wam::core::AppResolver(catalog, evidence).discoverTargets({local});
    const auto sample = std::find_if(targets.cbegin(), targets.cend(), [](const auto &target) {
        return target.application.id == QStringLiteral("sample-app");
    });
    QVERIFY(sample != targets.cend());
    QCOMPARE(sample->application.installState, wam::InstallState::Unknown);
    QCOMPARE(sample->application.confidence, 49);
    QCOMPARE(sample->application.attribution.state,
             wam::AttributionState::Verified);
    QCOMPARE(sample->application.attribution.confidence, 100);
    QCOMPARE(sample->application.installation.state,
             wam::InstallationState::Unknown);
    QVERIFY(std::any_of(sample->application.evidence.cbegin(),
                        sample->application.evidence.cend(), [](const auto &item) {
        return item.source == wam::EvidenceSource::Registry
                && item.status == wam::EvidenceStatus::Conflict;
    }));
    QVERIFY(std::none_of(targets.cbegin(), targets.cend(), [](const auto &target) {
        return target.application.installState == wam::InstallState::PotentialOrphan;
    }));
}

void BackendTest::resolverTreatsUnavailableEvidenceConservatively()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    QVERIFY(QDir().mkpath(QDir(local).filePath(QStringLiteral("Sample/App Data"))));

    QJsonObject rule = validRuleObject();
    rule.insert(QStringLiteral("executablePath"),
                QStringLiteral("%LOCALAPPDATA%/WAM-Evidence-Test-Missing/sample.exe"));
    rule.insert(QStringLiteral("identifiers"), QJsonObject {
        {QStringLiteral("registryDisplayNames"), QJsonArray {QStringLiteral("Sample App")}}
    });
    const auto catalog = wam::core::rules::RuleCatalog::fromJsonDocuments({
        {QStringLiteral("sample.json"), ruleJson(rule)}
    });

    wam::InstallationEvidenceSnapshot evidence;
    evidence.registry.issues.append(QStringLiteral("测试中的证据源不可用"));
    const auto targets = wam::core::AppResolver(catalog, evidence).discoverTargets({local});
    const auto sample = std::find_if(targets.cbegin(), targets.cend(), [](const auto &target) {
        return target.application.id == QStringLiteral("sample-app");
    });
    QVERIFY(sample != targets.cend());
    QCOMPARE(sample->application.installState, wam::InstallState::Unknown);
    QCOMPARE(sample->application.confidence, 72);
    QCOMPARE(sample->application.attribution.state,
             wam::AttributionState::Verified);
    QCOMPARE(sample->application.attribution.confidence, 100);
    QCOMPARE(sample->application.installation.state,
             wam::InstallationState::Unknown);
    QVERIFY(std::any_of(sample->application.evidence.cbegin(),
                        sample->application.evidence.cend(), [](const auto &item) {
        return item.source == wam::EvidenceSource::Registry
                && item.status == wam::EvidenceStatus::Unavailable
                && item.detail.contains(QStringLiteral("未降低"));
    }));
}

void BackendTest::resolverDoesNotPromoteUnknownFoldersFromInstallationEvidence()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QVERIFY(QDir().mkpath(QDir(temporary.path()).filePath(QStringLiteral("Sample App"))));

    wam::InstallationEvidenceSnapshot evidence;
    evidence.registry.availability = wam::InstallationEvidenceAvailability::Complete;
    evidence.registry.records.append({
        .displayName = QStringLiteral("Sample App"),
        .publisher = QStringLiteral("Sample Publisher")
    });

    const auto targets = wam::core::AppResolver(evidence).discoverTargets({temporary.path()});
    QCOMPARE(targets.size(), 1);
    QCOMPARE(targets.constFirst().application.installState, wam::InstallState::Unknown);
    QCOMPARE(targets.constFirst().application.confidence, 20);
    QCOMPARE(targets.constFirst().application.attribution.state,
             wam::AttributionState::Unknown);
    QCOMPARE(targets.constFirst().application.attribution.confidence, 20);
    QCOMPARE(targets.constFirst().application.installation.state,
             wam::InstallationState::Unknown);
    QVERIFY(targets.constFirst().application.installation.evidence.isEmpty());
    QCOMPARE(targets.constFirst().application.risk, wam::RiskLevel::Unknown);
}

void BackendTest::resolverPreservesUnknownCandidateEvidence()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString directory = QDir(temporary.path()).filePath(QStringLiteral("Sample App"));
    QVERIFY(QDir().mkpath(directory));

    wam::InstallationEvidenceSnapshot evidence;
    evidence.executable.availability = wam::InstallationEvidenceAvailability::Complete;
    evidence.executable.records.append({
        QStringLiteral("C:/Apps/Sample App/sample.exe"),
        wam::ExecutablePathState::Present,
        wam::VersionMetadataState::Available,
        QStringLiteral("Sample App"),
        QStringLiteral("Sample Publisher"),
        QStringLiteral("Sample App"),
        QStringLiteral("sample.exe"),
        wam::AuthenticodeState::Unsigned,
        {}
    });

    const auto targets = wam::core::AppResolver(evidence).discoverTargets(
            {temporary.path()});
    QCOMPARE(targets.size(), 1);
    const auto &application = targets.constFirst().application;
    QCOMPARE(application.attribution.state, wam::AttributionState::StrongInferred);
    QCOMPARE(application.installation.state, wam::InstallationState::Installed);
    QVERIFY(std::any_of(application.attribution.evidence.cbegin(),
                        application.attribution.evidence.cend(), [](const auto &item) {
        return item.source == wam::EvidenceSource::Executable
                && item.status == wam::EvidenceStatus::Matched;
    }));
    QVERIFY(std::any_of(application.attribution.evidence.cbegin(),
                        application.attribution.evidence.cend(), [](const auto &item) {
        return item.source == wam::EvidenceSource::Folder;
    }));
}

void BackendTest::candidateGeneratorRequiresIndependentInstallAnchor()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString directory = QDir(temporary.path()).filePath(QStringLiteral("Sample App"));

    wam::InstallationEvidenceSnapshot evidence;
    evidence.registry.records.append({
        .displayName = QStringLiteral("Sample App"),
        .publisher = QStringLiteral("Sample Publisher")
    });
    QVERIFY(wam::core::CandidateGenerator::generate(directory, evidence).isEmpty());

    evidence.registry.records[0].displayIcon =
            QStringLiteral("\"C:/Apps/Sample App/sample.ico\",0");
    const auto iconCandidates = wam::core::CandidateGenerator::generate(directory, evidence);
    QCOMPARE(iconCandidates.size(), 1);
    QVERIFY(std::any_of(iconCandidates.constFirst().attribution.evidence.cbegin(),
                        iconCandidates.constFirst().attribution.evidence.cend(),
                        [](const auto &item) {
        return item.source == wam::EvidenceSource::InstallPath
                && item.detail.contains(QStringLiteral("DisplayIcon"));
    }));

    evidence.registry.records[0].displayIcon.clear();
    evidence.registry.records[0].uninstallString =
            QStringLiteral("\"C:/Apps/Sample App/uninstall.exe\" /S");
    const auto uninstallCandidates = wam::core::CandidateGenerator::generate(
            directory, evidence);
    QCOMPARE(uninstallCandidates.size(), 1);
    QVERIFY(std::any_of(uninstallCandidates.constFirst().attribution.evidence.cbegin(),
                        uninstallCandidates.constFirst().attribution.evidence.cend(),
                        [](const auto &item) {
        return item.source == wam::EvidenceSource::InstallPath
                && item.detail.contains(QStringLiteral("卸载命令"));
    }));

    evidence.registry.records[0].uninstallString.clear();
    evidence.registry.records[0].installPath = QStringLiteral("C:/Apps/Sample App");
    const auto candidates = wam::core::CandidateGenerator::generate(directory, evidence);
    QCOMPARE(candidates.size(), 1);
    QCOMPARE(candidates.constFirst().attribution.state,
             wam::AttributionState::StrongInferred);
    QVERIFY(candidates.constFirst().attribution.evidence.contains({
        wam::EvidenceSource::Registry, wam::EvidenceStatus::Matched,
        QStringLiteral("注册表安装项提供候选名称“Sample App”")
    }));
    QVERIFY(candidates.constFirst().attribution.evidence.contains({
        wam::EvidenceSource::InstallPath, wam::EvidenceStatus::Matched,
        QStringLiteral("注册表 InstallLocation 提供安装目录")
    }));
}

void BackendTest::candidateGeneratorUsesAppxPackageIdentity()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString directory = QDir(temporary.path()).filePath(QStringLiteral("Sample App"));

    wam::InstallationEvidenceSnapshot evidence;
    evidence.appx.records.append({
        .packageName = QStringLiteral("Other.Package"),
        .publisher = QStringLiteral("CN=Other Publisher"),
        .packageFamilyName = QStringLiteral("Other.Package_family"),
        .displayName = QStringLiteral("Sample App")
    });
    QVERIFY(wam::core::CandidateGenerator::generate(directory, evidence).isEmpty());
    evidence.appx.records.clear();
    evidence.appx.records.append({
        .displayName = QStringLiteral("Sample App")
    });
    QVERIFY(wam::core::CandidateGenerator::generate(directory, evidence).isEmpty());
    evidence.appx.records.clear();
    evidence.appx.records.append({
        .packageFamilyName = QStringLiteral("Sample.App_family")
    });
    QVERIFY(!wam::core::CandidateGenerator::generate(directory, evidence).isEmpty());
    evidence.appx.records.clear();
    evidence.appx.records.append({
        .packageName = QStringLiteral("Sample.App"),
        .publisher = QStringLiteral("CN=Sample Publisher"),
        .packageFamilyName = QStringLiteral("Sample.App_family"),
        .displayName = QStringLiteral("Sample App")
    });
    const auto candidates = wam::core::CandidateGenerator::generate(directory, evidence);
    QCOMPARE(candidates.size(), 1);
    QCOMPARE(candidates.constFirst().name, QStringLiteral("Sample App"));
    QVERIFY(candidates.constFirst().attribution.state
            == wam::AttributionState::StrongInferred
            || candidates.constFirst().attribution.state
                    == wam::AttributionState::Suggested);
    QVERIFY(std::any_of(candidates.constFirst().attribution.evidence.cbegin(),
                        candidates.constFirst().attribution.evidence.cend(),
                        [](const auto &item) {
        return item.source == wam::EvidenceSource::Appx
                && item.status == wam::EvidenceStatus::Matched;
    }));
}

void BackendTest::candidateGeneratorUsesExecutableMetadata()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString directory = QDir(temporary.path()).filePath(QStringLiteral("Sample App"));

    wam::InstallationEvidenceSnapshot evidence;
    evidence.executable.availability = wam::InstallationEvidenceAvailability::Complete;
    evidence.executable.records.append({
        QStringLiteral("C:/Apps/Sample App/sample.exe"),
        wam::ExecutablePathState::Present,
        wam::VersionMetadataState::Available,
        QStringLiteral("Sample App"),
        QStringLiteral("Sample Publisher"),
        QStringLiteral("Sample App"),
        QStringLiteral("sample.exe"),
        wam::AuthenticodeState::Unsigned,
        {}
    });

    const auto candidates = wam::core::CandidateGenerator::generate(directory, evidence);
    QCOMPARE(candidates.size(), 1);
    const auto &candidate = candidates.constFirst();
    QCOMPARE(candidate.name, QStringLiteral("Sample App"));
    QCOMPARE(candidate.publisher, QStringLiteral("Sample Publisher"));
    QCOMPARE(candidate.attribution.state, wam::AttributionState::StrongInferred);
    QVERIFY(candidate.attribution.confidence >= 75);
    QVERIFY(std::any_of(candidate.attribution.evidence.cbegin(),
                        candidate.attribution.evidence.cend(), [](const auto &item) {
        return item.source == wam::EvidenceSource::Executable
                && item.status == wam::EvidenceStatus::Matched;
    }));
    QCOMPARE(candidate.installation.state, wam::InstallationState::Installed);
    QVERIFY(std::any_of(candidate.installation.evidence.cbegin(),
                        candidate.installation.evidence.cend(), [](const auto &item) {
        return item.source == wam::EvidenceSource::Executable
                && item.status == wam::EvidenceStatus::Matched;
    }));

    evidence.executable.records[0].metadataState = wam::VersionMetadataState::Missing;
    QVERIFY(wam::core::CandidateGenerator::generate(directory, evidence).isEmpty());
}

void BackendTest::candidateGeneratorUsesExecutableParentDirectory()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString directory = QDir(temporary.path()).filePath(QStringLiteral("Discord"));

    wam::InstallationEvidenceSnapshot evidence;
    evidence.executable.availability = wam::InstallationEvidenceAvailability::Complete;
    evidence.executable.records.append({
        QStringLiteral("C:/Apps/Discord/Update.exe"),
        wam::ExecutablePathState::Present,
        wam::VersionMetadataState::Available,
        QStringLiteral("Update"),
        QStringLiteral("Discord Inc."),
        QStringLiteral("Discord Updater"),
        QStringLiteral("Update.exe"),
        wam::AuthenticodeState::Unsigned,
        {}
    });

    const auto candidates = wam::core::CandidateGenerator::generate(directory, evidence);
    QCOMPARE(candidates.size(), 1);
    const auto &candidate = candidates.constFirst();
    QCOMPARE(candidate.name, QStringLiteral("Discord"));
    QCOMPARE(candidate.publisher, QStringLiteral("Discord Inc."));
    QCOMPARE(candidate.attribution.state, wam::AttributionState::StrongInferred);
    QVERIFY(std::any_of(candidate.attribution.evidence.cbegin(),
                        candidate.attribution.evidence.cend(), [](const auto &item) {
        return item.source == wam::EvidenceSource::Folder
                && item.status == wam::EvidenceStatus::Matched
                && item.detail.contains(QStringLiteral("父目录"));
    }));

    evidence.executable.records[0].productName = QStringLiteral("Different Product");
    evidence.executable.records[0].fileDescription = QStringLiteral("Different Product");
    const auto explicitMetadataCandidates =
            wam::core::CandidateGenerator::generate(directory, evidence);
    QCOMPARE(explicitMetadataCandidates.size(), 1);
    QCOMPARE(explicitMetadataCandidates.constFirst().name,
             QStringLiteral("Different Product"));
}

void BackendTest::candidateGeneratorUsesRunningProcessEvidenceInRanking()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString directory = QDir(temporary.path()).filePath(QStringLiteral("Sample App"));

    wam::InstallationEvidenceSnapshot withoutProcess;
    withoutProcess.executable.records.append({
        QStringLiteral("C:/Apps/Sample App/sample.exe"),
        wam::ExecutablePathState::Present,
        wam::VersionMetadataState::Available,
        QStringLiteral("Unrelated Product"),
        QStringLiteral("Sample Publisher"),
        QStringLiteral("Unrelated Product"),
        QStringLiteral("sample.exe"),
        wam::AuthenticodeState::Unsigned,
        {}
    });
    const auto baseCandidates =
            wam::core::CandidateGenerator::generate(directory, withoutProcess);
    QCOMPARE(baseCandidates.size(), 1);

    wam::InstallationEvidenceSnapshot withProcess = withoutProcess;
    withProcess.runningProcesses.records.append({
        .processId = 42,
        .imageName = QStringLiteral("sample.exe"),
        .imagePath = QStringLiteral("C:/Apps/Sample App/sample.exe")
    });
    const auto rankedCandidates =
            wam::core::CandidateGenerator::generate(directory, withProcess);
    QCOMPARE(rankedCandidates.size(), 1);
    QVERIFY(rankedCandidates.constFirst().score > baseCandidates.constFirst().score);
    QVERIFY(std::any_of(rankedCandidates.constFirst().installation.evidence.cbegin(),
                        rankedCandidates.constFirst().installation.evidence.cend(),
                        [](const auto &item) {
        return item.source == wam::EvidenceSource::RunningProcess
                && item.status == wam::EvidenceStatus::Matched;
    }));
    QVERIFY(std::any_of(rankedCandidates.constFirst().attribution.evidence.cbegin(),
                        rankedCandidates.constFirst().attribution.evidence.cend(),
                        [](const auto &item) {
        return item.source == wam::EvidenceSource::RunningProcess
                && item.status == wam::EvidenceStatus::Matched;
    }));
}

void BackendTest::candidateGeneratorMarksCloseCandidatesAmbiguous()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString directory = QDir(temporary.path()).filePath(QStringLiteral("Sample App"));

    wam::InstallationEvidenceSnapshot evidence;
    evidence.registry.records = {
        {
            .displayName = QStringLiteral("Sample App"),
            .publisher = QStringLiteral("Publisher One"),
            .installPath = QStringLiteral("C:/Apps/Sample App")
        },
        {
            .displayName = QStringLiteral("Sample App"),
            .publisher = QStringLiteral("Publisher Two"),
            .installPath = QStringLiteral("D:/Apps/Sample App")
        }
    };
    const auto candidates = wam::core::CandidateGenerator::generate(directory, evidence);
    QCOMPARE(candidates.size(), 2);
    QVERIFY(std::all_of(candidates.cbegin(), candidates.cend(), [](const auto &candidate) {
        return candidate.ambiguous
                && candidate.attribution.state == wam::AttributionState::Unknown;
    }));
}

void BackendTest::vendorNamespaceExpandsChildrenWithoutTreatingParentAsApplicationData()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    const QString vendor = QDir(local).filePath(QStringLiteral("Vendor"));
    QVERIFY(QDir().mkpath(QDir(vendor).filePath(QStringLiteral("Product One"))));
    QVERIFY(QDir().mkpath(QDir(vendor).filePath(QStringLiteral("Product Two"))));

    QJsonObject rule = validRuleObject(QStringLiteral("vendor-namespace"));
    rule.insert(QStringLiteral("name"), QStringLiteral("Vendor Namespace"));
    rule.insert(QStringLiteral("publisher"), QStringLiteral("Vendor"));
    rule.insert(QStringLiteral("locations"), QJsonArray {
        QJsonObject {
            {QStringLiteral("scope"), QStringLiteral("local")},
            {QStringLiteral("path"), QStringLiteral("Vendor")},
            {QStringLiteral("ownership"), QStringLiteral("shared")},
            {QStringLiteral("role"), QStringLiteral("vendor-namespace")}
        }
    });
    const auto catalog = wam::core::rules::RuleCatalog::fromJsonDocuments({
        {QStringLiteral("vendor.json"), ruleJson(rule)}
    });
    QVERIFY2(catalog.issues().isEmpty(), "VendorNamespace 规则必须通过校验");

    const auto targets = wam::core::AppResolver(catalog).discoverTargets({local});
    const auto parent = std::find_if(targets.cbegin(), targets.cend(), [](const auto &target) {
        return target.application.id == QStringLiteral("vendor-namespace");
    });
    QVERIFY(parent != targets.cend());
    QCOMPARE(parent->application.ownerKind, wam::OwnerKind::Vendor);
    QCOMPARE(parent->excludedPaths.size(), 2);
    QVERIFY(std::all_of(parent->excludedPaths.cbegin(), parent->excludedPaths.cend(),
                        [](const QString &path) {
        return path.contains(QStringLiteral("Product"), Qt::CaseInsensitive);
    }));

    const auto children = std::count_if(targets.cbegin(), targets.cend(), [](const auto &target) {
        return target.application.id != QStringLiteral("vendor-namespace")
                && target.path.contains(QStringLiteral("Vendor"), Qt::CaseInsensitive);
    });
    QCOMPARE(children, 2);
}

void BackendTest::resolverPublishesApplicationClassificationRules()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    const QString cache = QDir(local).filePath(QStringLiteral("discord/Cache"));
    const QString session = QDir(local).filePath(
            QStringLiteral("discord/Session Storage"));
    QVERIFY(QDir().mkpath(cache));
    QVERIFY(QDir().mkpath(session));
    writeFile(QDir(cache).filePath(QStringLiteral("cache.bin")), QByteArray(9, 'c'));
    writeFile(QDir(session).filePath(QStringLiteral("session.bin")), QByteArray(4, 's'));

    QJsonObject discordRule = validRuleObject(QStringLiteral("discord"));
    discordRule.insert(QStringLiteral("name"), QStringLiteral("Discord"));
    discordRule.insert(QStringLiteral("publisher"), QStringLiteral("Discord Inc."));
    discordRule.insert(QStringLiteral("applicationCategory"), QStringLiteral("通讯"));
    discordRule.insert(QStringLiteral("executablePath"),
                       QStringLiteral("%LOCALAPPDATA%/Discord/Update.exe"));
    discordRule.insert(QStringLiteral("installPath"),
                       QStringLiteral("%LOCALAPPDATA%/Discord"));
    discordRule.insert(QStringLiteral("locations"), QJsonArray {
        QJsonObject {
            {QStringLiteral("scope"), QStringLiteral("local")},
            {QStringLiteral("path"), QStringLiteral("discord")}
        }
    });
    discordRule.insert(QStringLiteral("entries"), QJsonArray {
        QJsonObject {
            {QStringLiteral("id"), QStringLiteral("cache")},
            {QStringLiteral("path"), QStringLiteral("Cache")},
            {QStringLiteral("category"), QStringLiteral("cache")},
            {QStringLiteral("risk"), QStringLiteral("safe")},
            {QStringLiteral("rebuildable"), true},
            {QStringLiteral("impact"), QStringLiteral("缓存可重新生成。")}
        },
        QJsonObject {
            {QStringLiteral("id"), QStringLiteral("session")},
            {QStringLiteral("path"), QStringLiteral("Session Storage")},
            {QStringLiteral("category"), QStringLiteral("session")},
            {QStringLiteral("risk"), QStringLiteral("high")},
            {QStringLiteral("rebuildable"), false},
            {QStringLiteral("impact"), QStringLiteral("会话状态需要保留。")}
        }
    });
    const auto catalog = wam::core::rules::RuleCatalog::fromJsonDocuments({
        {QStringLiteral("测试规则"), ruleJson(discordRule)}
    });
    QVERIFY(catalog.issues().isEmpty());

    const auto targets = wam::core::AppResolver(catalog).discoverTargets({local});
    QCOMPARE(targets.size(), 1);
    QCOMPARE(targets.constFirst().application.id, QStringLiteral("discord"));
    QCOMPARE(targets.constFirst().ruleOrigin, wam::RuleOrigin::Local);
    QCOMPARE(targets.constFirst().ruleTrustLevel, wam::RuleTrustLevel::Unverified);

    const wam::core::DataClassifier classifier;
    const auto cacheClassification = classifier.classify(
            fsPath(QStringLiteral("Cache/cache.bin")),
            targets.constFirst().classificationRules,
            targets.constFirst().ruleSource);
    QCOMPARE(cacheClassification.risk, wam::RiskLevel::Safe);
    QCOMPARE(cacheClassification.rebuildable, wam::RebuildableState::Yes);
    QVERIFY(cacheClassification.ruleSource.contains(QStringLiteral("discord@1")));

    const auto sessionClassification = classifier.classify(
            fsPath(QStringLiteral("Session Storage/session.bin")),
            targets.constFirst().classificationRules,
            targets.constFirst().ruleSource);
    QCOMPARE(sessionClassification.risk, wam::RiskLevel::High);
    QCOMPARE(sessionClassification.rebuildable, wam::RebuildableState::No);
}

QTEST_GUILESS_MAIN(BackendTest)

#include "tst_backend.moc"
