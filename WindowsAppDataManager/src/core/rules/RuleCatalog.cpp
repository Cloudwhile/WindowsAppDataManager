#include "RuleCatalog.h"
#include "IdentifierNormalization.h"
#include "RulePathResolver.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <utility>

static void initializeBuiltinRuleResources()
{
    Q_INIT_RESOURCE(wam_builtin_rules);
}

namespace wam::core::rules {
namespace {

struct IdentifierClaim {
    QString applicationId;
    QSet<QString> publishers;
};

using IdentifierClaims = QHash<QString, QVector<IdentifierClaim>>;

QSet<QString> normalizedPublishers(const QStringList &publishers)
{
    QSet<QString> result;
    for (const QString &publisher : publishers)
        result.insert(normalizedIdentifier(publisher));
    return result;
}

bool publisherClaimsOverlap(const QSet<QString> &left, const QSet<QString> &right)
{
    if (left.isEmpty() || right.isEmpty())
        return true;
    return std::any_of(left.cbegin(), left.cend(), [&right](const QString &publisher) {
        return right.contains(publisher);
    });
}

QString conflictingOwner(const QStringList &identifiers,
                         const QStringList &publishers,
                         const IdentifierClaims &claims)
{
    const QSet<QString> normalizedPublisherSet = normalizedPublishers(publishers);
    for (const QString &identifier : identifiers) {
        const auto existingClaims = claims.constFind(normalizedIdentifier(identifier));
        if (existingClaims == claims.cend())
            continue;
        for (const IdentifierClaim &claim : *existingClaims) {
            if (publisherClaimsOverlap(normalizedPublisherSet, claim.publishers))
                return claim.applicationId;
        }
    }
    return {};
}

void addClaims(const QStringList &identifiers,
               const QStringList &publishers,
               const QString &applicationId,
               IdentifierClaims &claims)
{
    const QSet<QString> normalizedPublisherSet = normalizedPublishers(publishers);
    for (const QString &identifier : identifiers) {
        claims[normalizedIdentifier(identifier)].append(
                {applicationId, normalizedPublisherSet});
    }
}

QStringList executablePathClaimKeys(const QString &path)
{
    QStringList keys;
    const QString declaredKey = normalizedRulePathClaim(path);
    if (!declaredKey.isEmpty())
        keys.append(QStringLiteral("declared:") + declaredKey);

    const RulePathResolution resolution = resolveRulePath(path);
    if (resolution.isResolved()) {
        const QString resolvedKey = normalizedPathKey(resolution.path);
        if (!resolvedKey.isEmpty())
            keys.append(QStringLiteral("resolved:") + resolvedKey);
    }
    return keys;
}

QStringList executablePathClaimKeys(const QStringList &paths)
{
    QStringList keys;
    for (const QString &path : paths)
        keys.append(executablePathClaimKeys(path));
    keys.removeDuplicates();
    return keys;
}

QString executablePathOwner(const QStringList &keys,
                            const QHash<QString, QString> &claims)
{
    for (const QString &key : keys) {
        const auto owner = claims.constFind(key);
        if (owner != claims.cend())
            return *owner;
    }
    return {};
}

} // namespace

RuleCatalog RuleCatalog::fromJsonDocuments(const QVector<RuleDocument> &documents)
{
    RuleCatalog catalog;
    QSet<QString> applicationIds;
    IdentifierClaims registryClaims;
    IdentifierClaims appxClaims;
    QHash<QString, QString> executablePathClaims;

    for (const RuleDocument &document : documents) {
        RuleLoadResult loadResult = RuleLoader::load(document.json, document.sourceName);
        catalog.m_issues += loadResult.issues;
        if (!loadResult.rule)
            continue;

        const QString normalizedId = loadResult.rule->id.toCaseFolded();
        if (applicationIds.contains(normalizedId)) {
            catalog.m_issues.append({
                RuleIssueCode::DuplicateId,
                document.sourceName,
                QStringLiteral("id"),
                QStringLiteral("应用规则标识重复，已跳过后加载的规则：%1")
                        .arg(loadResult.rule->id)
            });
            continue;
        }

        const QString registryOwner = conflictingOwner(
                loadResult.rule->identifiers.registryDisplayNames,
                loadResult.rule->identifiers.registryPublishers,
                registryClaims);
        if (!registryOwner.isEmpty()) {
            catalog.m_issues.append({
                RuleIssueCode::AmbiguousIdentifier,
                document.sourceName,
                QStringLiteral("identifiers.registryDisplayNames"),
                QStringLiteral("注册表主标识与应用 %1 的声明重叠，已跳过后加载的规则")
                        .arg(registryOwner)
            });
            continue;
        }

        const QString appxOwner = conflictingOwner(
                loadResult.rule->identifiers.appxPackageNames,
                loadResult.rule->identifiers.appxPublishers,
                appxClaims);
        if (!appxOwner.isEmpty()) {
            catalog.m_issues.append({
                RuleIssueCode::AmbiguousIdentifier,
                document.sourceName,
                QStringLiteral("identifiers.appxPackageNames"),
                QStringLiteral("AppX 主标识与应用 %1 的声明重叠，已跳过后加载的规则")
                        .arg(appxOwner)
            });
            continue;
        }

        const QStringList executablePathKeys = executablePathClaimKeys(
                loadResult.rule->executablePaths.isEmpty()
                        ? QStringList {loadResult.rule->executablePath}
                        : loadResult.rule->executablePaths);
        const QString executableOwner = executablePathOwner(
                executablePathKeys, executablePathClaims);
        if (!executableOwner.isEmpty()) {
            catalog.m_issues.append({
                RuleIssueCode::AmbiguousIdentifier,
                document.sourceName,
                QStringLiteral("executablePath"),
                QStringLiteral("可执行文件路径与应用 %1 的声明重复，已跳过后加载的规则")
                        .arg(executableOwner)
            });
            continue;
        }

        applicationIds.insert(normalizedId);
        addClaims(loadResult.rule->identifiers.registryDisplayNames,
                  loadResult.rule->identifiers.registryPublishers,
                  loadResult.rule->id,
                  registryClaims);
        addClaims(loadResult.rule->identifiers.appxPackageNames,
                  loadResult.rule->identifiers.appxPublishers,
                  loadResult.rule->id,
                  appxClaims);
        for (const QString &key : executablePathKeys)
            executablePathClaims.insert(key, loadResult.rule->id);
        catalog.m_applications.append(std::move(*loadResult.rule));
    }
    return catalog;
}

const RuleCatalog &RuleCatalog::builtIn()
{
    static const RuleCatalog catalog = [] {
        initializeBuiltinRuleResources();

        const QString resourceDirectoryPath =
                QStringLiteral(":/windowsappdatamanager/rules/builtin");
        const QDir resourceDirectory(resourceDirectoryPath);
        QVector<RuleDocument> documents;
        QVector<RuleLoadIssue> resourceIssues;
        if (!resourceDirectory.exists()
                || !QFileInfo(resourceDirectoryPath).isReadable()) {
            resourceIssues.append({
                RuleIssueCode::ResourceUnavailable,
                resourceDirectoryPath,
                QStringLiteral("$"),
                QStringLiteral("无法读取内置规则资源目录")
            });
        }

        const QStringList resourceNames = resourceDirectory.entryList(
                {QStringLiteral("*.json")},
                QDir::Files,
                QDir::Name | QDir::IgnoreCase);
        if (resourceIssues.isEmpty() && resourceNames.isEmpty()) {
            resourceIssues.append({
                RuleIssueCode::ResourceUnavailable,
                resourceDirectoryPath,
                QStringLiteral("$"),
                QStringLiteral("内置规则资源目录中没有 JSON 规则")
            });
        }

        documents.reserve(resourceNames.size());
        for (const QString &resourceName : resourceNames) {
            const QString resourcePath = resourceDirectory.filePath(resourceName);
            QFile file(resourcePath);
            if (!file.open(QIODevice::ReadOnly)) {
                resourceIssues.append({
                    RuleIssueCode::ResourceUnavailable,
                    resourcePath,
                    QStringLiteral("$"),
                    QStringLiteral("无法读取内置规则资源：%1").arg(file.errorString())
                });
                continue;
            }
            documents.append({resourcePath, file.readAll()});
        }

        RuleCatalog loaded = RuleCatalog::fromJsonDocuments(documents);
        loaded.m_issues = resourceIssues + loaded.m_issues;
        for (const RuleLoadIssue &issue : std::as_const(loaded.m_issues)) {
            qWarning().noquote() << QStringLiteral("规则加载警告 [%1] %2：%3")
                                    .arg(issue.source, issue.field, issue.message);
        }
        return loaded;
    }();
    return catalog;
}

const QVector<ApplicationRule> &RuleCatalog::applications() const
{
    return m_applications;
}

const QVector<RuleLoadIssue> &RuleCatalog::issues() const
{
    return m_issues;
}

const ApplicationRule *RuleCatalog::findById(const QString &applicationId) const
{
    const auto iterator = std::find_if(
            m_applications.cbegin(), m_applications.cend(),
            [&applicationId](const ApplicationRule &rule) {
        return rule.id.compare(applicationId, Qt::CaseInsensitive) == 0;
    });
    return iterator == m_applications.cend() ? nullptr : &*iterator;
}

} // namespace wam::core::rules
