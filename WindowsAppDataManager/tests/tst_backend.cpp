#include "src/core/classifier/DataClassifier.h"
#include "src/core/classifier/RiskAssessment.h"
#include "src/core/resolver/AppResolver.h"
#include "src/core/rules/RuleCatalog.h"
#include "src/core/rules/RuleLoader.h"
#include "src/core/scanner/DirectoryScanner.h"
#include "src/qmlmodels/ApplicationFilterModel.h"
#include "src/qmlmodels/ApplicationListModel.h"
#include "src/qmlmodels/ScanViewModel.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <filesystem>
#include <utility>

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

wam::ApplicationInfo application(QString id,
                                 QString name,
                                 QString publisher,
                                 QString category,
                                 quint64 size,
                                 wam::RiskLevel risk,
                                 wam::InstallState installState)
{
    wam::ApplicationInfo result;
    result.id = std::move(id);
    result.name = std::move(name);
    result.publisher = std::move(publisher);
    result.category = std::move(category);
    result.totalSize = size;
    result.risk = risk;
    result.installState = installState;
    return result;
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
    void ruleLoaderAcceptsInstallationIdentifiers();
    void ruleLoaderRejectsInvalidDocuments();
    void ruleLoaderRejectsInvalidInstallationIdentifiers();
    void ruleCatalogRejectsDuplicateApplicationIds();
    void ruleCatalogRejectsAmbiguousInstallationIdentifiers();
    void builtInCatalogContainsMvpApplications();
    void applicationRulesUseBoundaryAndLongestMatch();
    void applicationRiskPreservesSixLevels();
    void scannerHandlesUnicodeAndCountsFiles();
    void scannerHonorsCancellationAndExclusions();
    void resolverProducesStableCollisionFreeIds();
    void resolverSeparatesChromeAndChromium();
    void resolverExcludesNestedKnownTargets();
    void resolverUsesExactInstallationEvidence();
    void resolverRejectsMismatchedInstallationEvidence();
    void resolverTreatsUnavailableEvidenceConservatively();
    void resolverDoesNotPromoteUnknownFoldersFromInstallationEvidence();
    void applicationListFindsStableIdsAndExposesAccentIndices();
    void applicationFilterCombinesSearchAndExactFilters();
    void applicationFilterSortsAndMapsSourceRows();
    void viewModelPublishesBackgroundScan();
    void viewModelAppliesBuiltInRules();
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

void BackendTest::builtInCatalogContainsMvpApplications()
{
    const auto &catalog = wam::core::rules::RuleCatalog::builtIn();
    QVERIFY2(catalog.issues().isEmpty(), "内置规则必须全部通过加载校验");
    QCOMPARE(catalog.applications().size(), 7);
    QVERIFY(catalog.findById(QStringLiteral("google-chrome")));
    QVERIFY(catalog.findById(QStringLiteral("chromium")));
    QVERIFY(catalog.findById(QStringLiteral("discord")));
    QVERIFY(catalog.findById(QStringLiteral("visual-studio-code")));
    QVERIFY(catalog.findById(QStringLiteral("jetbrains")));
    QVERIFY(catalog.findById(QStringLiteral("windows-temp")));
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

    const auto longest = classifier.classify(
            fsPath(QStringLiteral("ASSETS/GENERATED/item.bin")), entries, source);
    QCOMPARE(longest.id, QStringLiteral("generated-assets"));
    QCOMPARE(longest.risk, wam::RiskLevel::Safe);
    QCOMPARE(longest.ruleSource, source);

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

void BackendTest::scannerHonorsCancellationAndExclusions()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString excluded = QDir(temporary.path()).filePath(QStringLiteral("Known/Data"));
    QVERIFY(QDir().mkpath(excluded));
    writeFile(QDir(excluded).filePath(QStringLiteral("excluded.bin")), QByteArray(10, 'x'));
    writeFile(QDir(temporary.path()).filePath(QStringLiteral("included.bin")), QByteArray(3, 'y'));

    std::atomic_bool cancelled = false;
    const auto stats = wam::core::DirectoryScanner().scan(
            temporary.path(), cancelled, {}, {}, {excluded});
    QCOMPARE(stats.fileCount, quint64(1));
    QCOMPARE(stats.totalSize, quint64(3));

    cancelled.store(true);
    const auto cancelledStats = wam::core::DirectoryScanner().scan(
            temporary.path(), cancelled, {}, {});
    QVERIFY(cancelledStats.cancelled);
    QCOMPARE(cancelledStats.fileCount, quint64(0));
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
    QCOMPARE(targets.constFirst().application.risk, wam::RiskLevel::Unknown);
}

void BackendTest::applicationListFindsStableIdsAndExposesAccentIndices()
{
    using wam::InstallState;
    using wam::RiskLevel;

    wam::qmlmodels::ApplicationListModel applications;
    applications.setApplications({
        application(QStringLiteral("alpha"), QStringLiteral("Alpha"),
                    QStringLiteral("Vendor"), QStringLiteral("工具"), 10,
                    RiskLevel::Safe, InstallState::Installed),
        application(QStringLiteral("beta"), QStringLiteral("Beta"),
                    QStringLiteral("Vendor"), QStringLiteral("工具"), 20,
                    RiskLevel::Low, InstallState::Installed),
        application(QStringLiteral("gamma"), QStringLiteral("Gamma"),
                    QStringLiteral("Vendor"), QStringLiteral("工具"), 30,
                    RiskLevel::Unknown, InstallState::Unknown)
    });

    QCOMPARE(applications.indexOfId(QStringLiteral("gamma")), 0);
    QCOMPARE(applications.indexOfId(QStringLiteral("beta")), 1);
    QCOMPARE(applications.indexOfId(QStringLiteral("alpha")), 2);
    QCOMPARE(applications.indexOfId(QStringLiteral("missing")), -1);
    QCOMPARE(applications.indexOfId(QString()), -1);

    QCOMPARE(applications.roleNames().value(
                     wam::qmlmodels::ApplicationListModel::AccentIndexRole),
             QByteArray("accentIndex"));
    for (int row = 0; row < applications.count(); ++row) {
        const QModelIndex index = applications.index(row, 0);
        const QVariant accentIndex = applications.data(
                index, wam::qmlmodels::ApplicationListModel::AccentIndexRole);
        QCOMPARE(accentIndex.metaType(), QMetaType::fromType<int>());
        QVERIFY(accentIndex.toInt() >= 0);
        QVERIFY(accentIndex.toInt() < 6);

        const QVariant mappedAccentIndex = applications.get(row).value(
                QStringLiteral("accentIndex"));
        QCOMPARE(mappedAccentIndex.metaType(), QMetaType::fromType<int>());
        QCOMPARE(mappedAccentIndex, accentIndex);
    }

    applications.setApplications({
        application(QStringLiteral("alpha"), QStringLiteral("Alpha"),
                    QStringLiteral("Vendor"), QStringLiteral("工具"), 50,
                    RiskLevel::Safe, InstallState::Installed),
        application(QStringLiteral("beta"), QStringLiteral("Beta"),
                    QStringLiteral("Vendor"), QStringLiteral("工具"), 20,
                    RiskLevel::Low, InstallState::Installed),
        application(QStringLiteral("gamma"), QStringLiteral("Gamma"),
                    QStringLiteral("Vendor"), QStringLiteral("工具"), 30,
                    RiskLevel::Unknown, InstallState::Unknown)
    });

    QCOMPARE(applications.indexOfId(QStringLiteral("alpha")), 0);
    QCOMPARE(applications.indexOfId(QStringLiteral("gamma")), 1);
    QCOMPARE(applications.indexOfId(QStringLiteral("beta")), 2);
}

void BackendTest::applicationFilterCombinesSearchAndExactFilters()
{
    using wam::InstallState;
    using wam::RiskLevel;

    wam::qmlmodels::ApplicationListModel applications;
    applications.setApplications({
        application(QStringLiteral("alpha"), QStringLiteral("Alpha Editor"),
                    QStringLiteral("Northwind"), QStringLiteral("开发工具"), 120,
                    RiskLevel::Low, InstallState::Installed),
        application(QStringLiteral("browser"), QStringLiteral("Browser Cache"),
                    QStringLiteral("Contoso"), QStringLiteral("浏览器"), 90,
                    RiskLevel::High, InstallState::PotentialOrphan),
        application(QStringLiteral("gamma"), QStringLiteral("Gamma Extension"),
                    QStringLiteral("Contoso"), QStringLiteral("扩展"), 40,
                    RiskLevel::High, InstallState::Installed)
    });

    wam::qmlmodels::ApplicationFilterModel filter(&applications);
    QCOMPARE(filter.count(), 3);

    const int alphaIndex = applications.indexOfId(QStringLiteral("alpha"));
    const int browserIndex = applications.indexOfId(QStringLiteral("browser"));
    const int gammaIndex = applications.indexOfId(QStringLiteral("gamma"));
    QVERIFY(alphaIndex >= 0);
    QVERIFY(browserIndex >= 0);
    QVERIFY(gammaIndex >= 0);
    QVERIFY(filter.containsSourceIndex(alphaIndex));
    QVERIFY(filter.containsSourceIndex(browserIndex));
    QVERIFY(filter.containsSourceIndex(gammaIndex));
    QVERIFY(!filter.containsSourceIndex(-1));
    QVERIFY(!filter.containsSourceIndex(applications.count()));

    filter.setSearchText(QStringLiteral("contoso"));
    QCOMPARE(filter.count(), 2);
    QVERIFY(!filter.containsSourceIndex(alphaIndex));
    QVERIFY(filter.containsSourceIndex(browserIndex));
    QVERIFY(filter.containsSourceIndex(gammaIndex));
    filter.setRiskFilter(static_cast<int>(RiskLevel::High));
    QCOMPARE(filter.count(), 2);
    filter.setInstallStateFilter(static_cast<int>(InstallState::PotentialOrphan));
    QCOMPARE(filter.count(), 1);
    QVERIFY(!filter.containsSourceIndex(alphaIndex));
    QVERIFY(filter.containsSourceIndex(browserIndex));
    QVERIFY(!filter.containsSourceIndex(gammaIndex));
    QCOMPARE(filter.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("browser"));

    filter.setSearchText(QStringLiteral("开发"));
    QCOMPARE(filter.count(), 0);
    filter.setRiskFilter(-1);
    filter.setInstallStateFilter(-1);
    QCOMPARE(filter.count(), 1);
    QVERIFY(filter.containsSourceIndex(alphaIndex));
    QVERIFY(!filter.containsSourceIndex(browserIndex));
    QVERIFY(!filter.containsSourceIndex(gammaIndex));
    QCOMPARE(filter.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("alpha"));

    filter.setRiskFilter(99);
    filter.setInstallStateFilter(-2);
    QCOMPARE(filter.riskFilter(), -1);
    QCOMPARE(filter.installStateFilter(), -1);
}

void BackendTest::applicationFilterSortsAndMapsSourceRows()
{
    using wam::InstallState;
    using wam::RiskLevel;

    wam::qmlmodels::ApplicationListModel applications;
    applications.setApplications({
        application(QStringLiteral("safe"), QStringLiteral("Zulu Safe"),
                    QStringLiteral("Vendor"), QStringLiteral("工具"), 20,
                    RiskLevel::Safe, InstallState::Installed),
        application(QStringLiteral("unknown"), QStringLiteral("Echo Unknown"),
                    QStringLiteral("Vendor"), QStringLiteral("工具"), 60,
                    RiskLevel::Unknown, InstallState::Unknown),
        application(QStringLiteral("protected"), QStringLiteral("Bravo Protected"),
                    QStringLiteral("Vendor"), QStringLiteral("工具"), 80,
                    RiskLevel::Protected, InstallState::Installed),
        application(QStringLiteral("high"), QStringLiteral("Alpha High"),
                    QStringLiteral("Vendor"), QStringLiteral("工具"), 80,
                    RiskLevel::High, InstallState::PotentialOrphan)
    });

    wam::qmlmodels::ApplicationFilterModel filter(&applications);
    QCOMPARE(filter.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("high"));
    QCOMPARE(filter.get(1).value(QStringLiteral("appId")).toString(),
             QStringLiteral("protected"));

    filter.setSortDescending(false);
    QCOMPARE(filter.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("safe"));

    filter.setSortMode(1);
    QCOMPARE(filter.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("high"));
    filter.setSortDescending(true);
    QCOMPARE(filter.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("safe"));

    filter.setSortMode(2);
    QCOMPARE(filter.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("protected"));
    QCOMPARE(filter.get(1).value(QStringLiteral("appId")).toString(),
             QStringLiteral("high"));
    QCOMPARE(filter.get(2).value(QStringLiteral("appId")).toString(),
             QStringLiteral("unknown"));

    for (int proxyIndex = 0; proxyIndex < filter.count(); ++proxyIndex) {
        const QVariantMap item = filter.get(proxyIndex);
        const int sourceIndex = item.value(QStringLiteral("sourceIndex")).toInt();
        QCOMPARE(applications.get(sourceIndex).value(QStringLiteral("appId")),
                 item.value(QStringLiteral("appId")));
    }
}

void BackendTest::viewModelPublishesBackgroundScan()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString cache = QDir(temporary.path()).filePath(QStringLiteral("示例应用/Cache"));
    const QString credentials = QDir(temporary.path()).filePath(
            QStringLiteral("示例应用/Login Data"));
    QVERIFY(QDir().mkpath(cache));
    QVERIFY(QDir().mkpath(credentials));
    writeFile(QDir(cache).filePath(QStringLiteral("cache.bin")), QByteArray(9, 'c'));
    writeFile(QDir(credentials).filePath(QStringLiteral("account.db")), QByteArray(4, 'd'));

    const QByteArray previousRoot = qgetenv("WAM_SCAN_ROOT");
    qputenv("WAM_SCAN_ROOT", temporary.path().toUtf8());

    wam::qmlmodels::ApplicationListModel applications;
    wam::qmlmodels::ScanViewModel scan(&applications);
    QSignalSpy revisionSpy(&applications, &wam::qmlmodels::ApplicationListModel::revisionChanged);
    scan.startScan();
    QVERIFY(revisionSpy.wait(5000));

    QVERIFY(!scan.running());
    QCOMPARE(scan.progress(), 100);
    QCOMPARE(applications.count(), 1);
    const QVariantMap application = applications.get(0);
    QCOMPARE(application.value(QStringLiteral("riskLevel")).toInt(),
             static_cast<int>(wam::RiskLevel::Unknown));
    QCOMPARE(application.value(QStringLiteral("reclaimableText")).toString(),
             QStringLiteral("0 B"));
    QCOMPARE(application.value(QStringLiteral("unknownSizeText")).toString(),
             QStringLiteral("13 B"));

    if (previousRoot.isNull())
        qunsetenv("WAM_SCAN_ROOT");
    else
        qputenv("WAM_SCAN_ROOT", previousRoot);
}

void BackendTest::viewModelAppliesBuiltInRules()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString roaming = QDir(temporary.path()).filePath(QStringLiteral("Roaming"));
    const QString cache = QDir(roaming).filePath(QStringLiteral("discord/Cache"));
    const QString session = QDir(roaming).filePath(
            QStringLiteral("discord/Session Storage"));
    QVERIFY(QDir().mkpath(cache));
    QVERIFY(QDir().mkpath(session));
    writeFile(QDir(cache).filePath(QStringLiteral("cache.bin")), QByteArray(9, 'c'));
    writeFile(QDir(session).filePath(QStringLiteral("session.bin")), QByteArray(4, 's'));

    const QByteArray previousRoot = qgetenv("WAM_SCAN_ROOT");
    qputenv("WAM_SCAN_ROOT", roaming.toUtf8());

    wam::qmlmodels::ApplicationListModel applications;
    wam::qmlmodels::ScanViewModel scan(&applications);
    QSignalSpy revisionSpy(&applications, &wam::qmlmodels::ApplicationListModel::revisionChanged);
    scan.startScan();
    QVERIFY(revisionSpy.wait(5000));

    QCOMPARE(applications.count(), 1);
    const QVariantMap application = applications.get(0);
    QCOMPARE(application.value(QStringLiteral("appId")).toString(),
             QStringLiteral("discord"));
    QCOMPARE(application.value(QStringLiteral("riskLevel")).toInt(),
             static_cast<int>(wam::RiskLevel::High));
    QCOMPARE(application.value(QStringLiteral("reclaimableText")).toString(),
             QStringLiteral("9 B"));
    QCOMPARE(application.value(QStringLiteral("unknownSizeText")).toString(),
             QStringLiteral("0 B"));

    const QVariantList groups = application.value(QStringLiteral("dataGroups")).toList();
    const auto cacheGroup = std::find_if(groups.cbegin(), groups.cend(), [](const QVariant &value) {
        return value.toMap().value(QStringLiteral("groupId")).toString()
                == QStringLiteral("cache");
    });
    QVERIFY(cacheGroup != groups.cend());
    QCOMPARE(cacheGroup->toMap().value(QStringLiteral("riskLevel")).toInt(),
             static_cast<int>(wam::RiskLevel::Safe));
    QVERIFY(cacheGroup->toMap().value(QStringLiteral("ruleSource")).toString()
                    .contains(QStringLiteral("内置规则 / discord@1")));

    if (previousRoot.isNull())
        qunsetenv("WAM_SCAN_ROOT");
    else
        qputenv("WAM_SCAN_ROOT", previousRoot);
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
