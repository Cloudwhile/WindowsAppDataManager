#include "AppResolver.h"
#include "../rules/IdentifierNormalization.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <optional>
#include <tuple>
#include <utility>

namespace wam::core {
namespace {

enum class InstallationMatchStatus {
    NotConfigured,
    Matched,
    Ambiguous,
    NotFound,
    Conflict,
    Incomplete,
    Unavailable
};

struct InstallationCandidate {
    QString identity;
    QString primary;
    QString publisher;
    QString installPath;
    QString label;
};

struct InstallationMatch {
    InstallationMatchStatus status = InstallationMatchStatus::NotConfigured;
    InstallationCandidate candidate;
    int candidateCount = 0;
    bool sourceIncomplete = false;
};

QString normalizedIdentity(const QString &value)
{
    return rules::normalizedIdentifier(value);
}

bool matchesAnyIdentity(const QString &value, const QStringList &expected)
{
    const QString normalized = normalizedIdentity(value);
    return std::any_of(expected.cbegin(), expected.cend(), [&normalized](const QString &item) {
        return normalizedIdentity(item) == normalized;
    });
}

QString normalizedInstallPath(const QString &value)
{
    if (value.trimmed().isEmpty())
        return {};
    return QDir::fromNativeSeparators(QDir::cleanPath(value)).toCaseFolded();
}

auto candidateSortKey(const InstallationCandidate &candidate)
{
    return std::tuple(normalizedIdentity(candidate.primary),
                      normalizedIdentity(candidate.publisher),
                      normalizedInstallPath(candidate.installPath),
                      normalizedIdentity(candidate.label),
                      normalizedIdentity(candidate.identity));
}

bool sameEffectiveCandidate(const InstallationCandidate &left,
                            const InstallationCandidate &right)
{
    return normalizedIdentity(left.primary) == normalizedIdentity(right.primary)
            && normalizedIdentity(left.publisher) == normalizedIdentity(right.publisher)
            && normalizedInstallPath(left.installPath)
                    == normalizedInstallPath(right.installPath);
}

QVector<InstallationCandidate> registryCandidates(
        const QVector<RegistryInstallationRecord> &records)
{
    QVector<InstallationCandidate> candidates;
    candidates.reserve(records.size());
    for (const RegistryInstallationRecord &record : records) {
        candidates.append({record.identity, record.displayName, record.publisher,
                           record.installPath, record.displayName});
    }
    return candidates;
}

QVector<InstallationCandidate> appxCandidates(
        const QVector<AppxInstallationRecord> &records)
{
    QVector<InstallationCandidate> candidates;
    candidates.reserve(records.size());
    for (const AppxInstallationRecord &record : records) {
        const QString label = !record.displayName.isEmpty() ? record.displayName
                : !record.packageFamilyName.isEmpty() ? record.packageFamilyName
                                                      : record.packageName;
        candidates.append({record.packageFamilyName, record.packageName, record.publisher,
                           record.installPath, label});
    }
    return candidates;
}

InstallationMatch matchInstallationRecords(
        const QStringList &identities,
        const QStringList &publishers,
        QVector<InstallationCandidate> candidates,
        InstallationEvidenceAvailability availability)
{
    // A publisher narrows a primary identity; it can never identify an app alone.
    if (identities.isEmpty())
        return {};

    std::sort(candidates.begin(), candidates.end(), [](const auto &left, const auto &right) {
        return candidateSortKey(left) < candidateSortKey(right);
    });
    candidates.erase(std::unique(candidates.begin(), candidates.end(), sameEffectiveCandidate),
                     candidates.end());

    QVector<InstallationCandidate> primaryMatches;
    QVector<InstallationCandidate> completeMatches;
    for (const InstallationCandidate &candidate : std::as_const(candidates)) {
        const bool identityMatches = matchesAnyIdentity(candidate.primary, identities);
        if (!identityMatches)
            continue;
        primaryMatches.append(candidate);
        const bool publisherMatches = publishers.isEmpty()
                || matchesAnyIdentity(candidate.publisher, publishers);
        if (publisherMatches)
            completeMatches.append(candidate);
    }

    const bool incomplete = availability != InstallationEvidenceAvailability::Complete;
    if (completeMatches.size() == 1) {
        return {InstallationMatchStatus::Matched, completeMatches.constFirst(), 1,
                incomplete};
    }
    if (completeMatches.size() > 1) {
        return {InstallationMatchStatus::Ambiguous, completeMatches.constFirst(),
                static_cast<int>(completeMatches.size()), incomplete};
    }
    if (!primaryMatches.isEmpty() && !publishers.isEmpty()) {
        return {InstallationMatchStatus::Conflict, primaryMatches.constFirst(),
                static_cast<int>(primaryMatches.size()), incomplete};
    }

    switch (availability) {
    case InstallationEvidenceAvailability::Complete:
        return {InstallationMatchStatus::NotFound, {}, 0, false};
    case InstallationEvidenceAvailability::Partial:
        return {InstallationMatchStatus::Incomplete, {}, 0, true};
    case InstallationEvidenceAvailability::Unavailable:
        return {InstallationMatchStatus::Unavailable, {}, 0, true};
    }
    return {InstallationMatchStatus::Unavailable, {}, 0, true};
}

EvidenceStatus evidenceStatus(InstallationMatchStatus status)
{
    switch (status) {
    case InstallationMatchStatus::Matched: return EvidenceStatus::Matched;
    case InstallationMatchStatus::Ambiguous: return EvidenceStatus::Ambiguous;
    case InstallationMatchStatus::NotFound: return EvidenceStatus::NotFound;
    case InstallationMatchStatus::Conflict: return EvidenceStatus::Conflict;
    case InstallationMatchStatus::Incomplete: return EvidenceStatus::Incomplete;
    case InstallationMatchStatus::Unavailable:
    case InstallationMatchStatus::NotConfigured:
        return EvidenceStatus::Unavailable;
    }
    return EvidenceStatus::Unavailable;
}

QString registryEvidenceDetail(const InstallationMatch &match)
{
    switch (match.status) {
    case InstallationMatchStatus::Matched:
        return QStringLiteral("已精确匹配注册表安装项“%1”").arg(match.candidate.label);
    case InstallationMatchStatus::Ambiguous:
        return QStringLiteral("找到 %1 个精确匹配的注册表安装项，未选择不确定的安装路径")
                .arg(match.candidateCount);
    case InstallationMatchStatus::NotFound:
        return QStringLiteral("完整枚举中未找到规则声明的注册表安装项，不能据此判定为残留");
    case InstallationMatchStatus::Conflict:
        return QStringLiteral("注册表显示名称已命中，但发布者与规则不一致");
    case InstallationMatchStatus::Incomplete:
        return QStringLiteral("注册表安装信息仅部分可用，未获得可用于否定安装状态的完整证据");
    case InstallationMatchStatus::Unavailable:
        return QStringLiteral("注册表安装信息当前不可用，未降低目录归属置信度");
    case InstallationMatchStatus::NotConfigured:
        return {};
    }
    return {};
}

QString appxEvidenceDetail(const InstallationMatch &match)
{
    switch (match.status) {
    case InstallationMatchStatus::Matched:
        return QStringLiteral("已精确匹配 AppX / MSIX 包“%1”").arg(match.candidate.label);
    case InstallationMatchStatus::Ambiguous:
        return QStringLiteral("找到 %1 个精确匹配的 AppX / MSIX 包，未选择不确定的安装路径")
                .arg(match.candidateCount);
    case InstallationMatchStatus::NotFound:
        return QStringLiteral("完整枚举中未找到规则声明的 AppX / MSIX 包，不能据此判定为残留");
    case InstallationMatchStatus::Conflict:
        return QStringLiteral("AppX / MSIX 包名已命中，但发布者与规则不一致");
    case InstallationMatchStatus::Incomplete:
        return QStringLiteral("AppX / MSIX 包信息仅部分可用，未获得可用于否定安装状态的完整证据");
    case InstallationMatchStatus::Unavailable:
        return QStringLiteral("AppX / MSIX 包信息当前不可用，未降低目录归属置信度");
    case InstallationMatchStatus::NotConfigured:
        return {};
    }
    return {};
}

QString rootScopeName(const QString &root)
{
    const QString name = QFileInfo(root).fileName().toCaseFolded();
    if (name == QStringLiteral("local"))
        return QStringLiteral("local");
    if (name == QStringLiteral("roaming"))
        return QStringLiteral("roaming");
    if (name == QStringLiteral("locallow"))
        return QStringLiteral("locallow");
    return QStringLiteral("custom");
}

std::optional<RuleScope> rootRuleScope(const QString &root)
{
    const QString scope = rootScopeName(root);
    if (scope == QStringLiteral("local"))
        return RuleScope::Local;
    if (scope == QStringLiteral("roaming"))
        return RuleScope::Roaming;
    if (scope == QStringLiteral("locallow"))
        return RuleScope::LocalLow;
    return std::nullopt;
}

QString scopeName(RuleScope scope)
{
    switch (scope) {
    case RuleScope::Local: return QStringLiteral("local");
    case RuleScope::Roaming: return QStringLiteral("roaming");
    case RuleScope::LocalLow: return QStringLiteral("locallow");
    }
    return QStringLiteral("unknown");
}

QString normalizedAbsolutePath(const QString &path)
{
    return QDir::fromNativeSeparators(
            QDir::cleanPath(QFileInfo(path).absoluteFilePath())).toCaseFolded();
}

QString expandEnvironmentPath(QString value)
{
    static const QStringList variables {
        QStringLiteral("LOCALAPPDATA"),
        QStringLiteral("APPDATA"),
        QStringLiteral("USERPROFILE"),
        QStringLiteral("SystemRoot"),
        QStringLiteral("ProgramFiles"),
        QStringLiteral("ProgramFiles(x86)")
    };
    for (const QString &variable : variables) {
        const QString replacement = qEnvironmentVariable(variable.toUtf8().constData());
        if (!replacement.isEmpty()) {
            value.replace(QLatin1Char('%') + variable + QLatin1Char('%'), replacement,
                          Qt::CaseInsensitive);
        }
    }
    return QDir::cleanPath(value);
}

QString validatedEvidenceInstallPath(const QString &value)
{
    const QString expanded = expandEnvironmentPath(value.trimmed());
    if (!QFileInfo(expanded).isAbsolute())
        return {};
    return QDir::toNativeSeparators(QDir::cleanPath(expanded));
}

QString ruleSource(const ApplicationRule &rule)
{
    const QString source = rule.sourceName.startsWith(QStringLiteral(":/"))
            ? QStringLiteral("内置规则") : rule.sourceName;
    return QStringLiteral("%1 / %2@%3").arg(source, rule.id, rule.version);
}

QString targetId(const QString &scope, const QString &path)
{
    const QByteArray digest = QCryptographicHash::hash(
            normalizedAbsolutePath(path).toUtf8(), QCryptographicHash::Sha256)
                                      .toHex().left(16);
    return QStringLiteral("unknown-%1-%2").arg(scope, QString::fromLatin1(digest));
}

ApplicationInfo knownApplicationInfo(const ApplicationRule &rule,
                                     const RuleLocation &location,
                                     const QString &path,
                                     const InstallationEvidenceSnapshot &evidence)
{
    ApplicationInfo application;
    application.id = rule.id;
    application.name = rule.name;
    application.publisher = rule.publisher;
    application.category = rule.category;
    application.location = QDir::toNativeSeparators(path);
    application.executablePath = QDir::toNativeSeparators(
            expandEnvironmentPath(rule.executablePath));
    application.installPath = QDir::toNativeSeparators(
            expandEnvironmentPath(rule.installPath));

    const bool executableExists = QFileInfo::exists(application.executablePath);
    const InstallationMatch registryMatch = matchInstallationRecords(
            rule.identifiers.registryDisplayNames,
            rule.identifiers.registryPublishers,
            registryCandidates(evidence.registry.records),
            evidence.registry.availability);
    const InstallationMatch appxMatch = matchInstallationRecords(
            rule.identifiers.appxPackageNames,
            rule.identifiers.appxPublishers,
            appxCandidates(evidence.appx.records),
            evidence.appx.availability);
    const bool registryMatched = registryMatch.status == InstallationMatchStatus::Matched;
    const bool appxMatched = appxMatch.status == InstallationMatchStatus::Matched;
    const bool registryPositive = registryMatched
            || registryMatch.status == InstallationMatchStatus::Ambiguous;
    const bool appxPositive = appxMatched
            || appxMatch.status == InstallationMatchStatus::Ambiguous;
    const int strongEvidenceCount = static_cast<int>(executableExists)
            + static_cast<int>(registryPositive) + static_cast<int>(appxPositive);
    const bool hasAmbiguity = registryMatch.status == InstallationMatchStatus::Ambiguous
            || appxMatch.status == InstallationMatchStatus::Ambiguous;
    const bool hasConflict = registryMatch.status == InstallationMatchStatus::Conflict
            || appxMatch.status == InstallationMatchStatus::Conflict;

    application.installState = strongEvidenceCount > 0
            ? InstallState::Installed : InstallState::Unknown;
    if (strongEvidenceCount >= 2)
        application.confidence = 98;
    else if (appxMatched)
        application.confidence = 96;
    else if (registryMatched)
        application.confidence = 94;
    else if (executableExists)
        application.confidence = 92;
    else if (registryPositive || appxPositive)
        application.confidence = 90;
    else
        application.confidence = 72;
    if (hasAmbiguity)
        application.confidence = std::min(application.confidence, 96);
    if (hasConflict) {
        application.confidence = strongEvidenceCount == 0
                ? 49 : std::min(application.confidence, 96);
    }
    application.risk = RiskLevel::Caution;

    const QString registryInstallPath = validatedEvidenceInstallPath(
            registryMatch.candidate.installPath);
    const QString appxInstallPath = validatedEvidenceInstallPath(
            appxMatch.candidate.installPath);
    if (registryMatched && !registryInstallPath.isEmpty())
        application.installPath = registryInstallPath;
    else if (appxMatched && !appxInstallPath.isEmpty())
        application.installPath = appxInstallPath;

    application.evidence.append({
        EvidenceSource::Folder,
        EvidenceStatus::Matched,
        QStringLiteral("已匹配 %1 范围内的 %2")
                .arg(scopeName(location.scope), location.relativePath)
    });
    application.evidence.append({
        EvidenceSource::Rule,
        EvidenceStatus::Matched,
        ruleSource(rule)
    });
    application.evidence.append({
        EvidenceSource::Executable,
        executableExists ? EvidenceStatus::Matched : EvidenceStatus::NotFound,
        executableExists ? QStringLiteral("预期可执行文件存在")
                         : QStringLiteral("未找到预期可执行文件，不能据此判断为残留")
    });

    if (registryMatch.status != InstallationMatchStatus::NotConfigured) {
        application.evidence.append({
            EvidenceSource::Registry,
            evidenceStatus(registryMatch.status),
            registryEvidenceDetail(registryMatch)
        });
        if (registryMatched && !rule.identifiers.registryPublishers.isEmpty()) {
            application.evidence.append({
                EvidenceSource::Publisher,
                EvidenceStatus::Matched,
                QStringLiteral("注册表发布者“%1”与规则精确一致")
                        .arg(registryMatch.candidate.publisher)
            });
        } else if (registryMatch.status == InstallationMatchStatus::Conflict) {
            application.evidence.append({
                EvidenceSource::Publisher,
                EvidenceStatus::Conflict,
                QStringLiteral("注册表发布者“%1”不在规则允许列表中")
                        .arg(registryMatch.candidate.publisher)
            });
        }
        if (registryMatch.sourceIncomplete
                && registryMatch.status != InstallationMatchStatus::Incomplete
                && registryMatch.status != InstallationMatchStatus::Unavailable) {
            application.evidence.append({
                EvidenceSource::Registry,
                EvidenceStatus::Incomplete,
                QStringLiteral("注册表来源未完整枚举；已观察到的证据仍保留")
            });
        }
    }

    if (appxMatch.status != InstallationMatchStatus::NotConfigured) {
        application.evidence.append({
            EvidenceSource::Appx,
            evidenceStatus(appxMatch.status),
            appxEvidenceDetail(appxMatch)
        });
        if (appxMatched && !rule.identifiers.appxPublishers.isEmpty()) {
            application.evidence.append({
                EvidenceSource::Publisher,
                EvidenceStatus::Matched,
                QStringLiteral("AppX / MSIX 发布者“%1”与规则精确一致")
                        .arg(appxMatch.candidate.publisher)
            });
        } else if (appxMatch.status == InstallationMatchStatus::Conflict) {
            application.evidence.append({
                EvidenceSource::Publisher,
                EvidenceStatus::Conflict,
                QStringLiteral("AppX / MSIX 发布者“%1”不在规则允许列表中")
                        .arg(appxMatch.candidate.publisher)
            });
        }
        if (appxMatch.sourceIncomplete
                && appxMatch.status != InstallationMatchStatus::Incomplete
                && appxMatch.status != InstallationMatchStatus::Unavailable) {
            application.evidence.append({
                EvidenceSource::Appx,
                EvidenceStatus::Incomplete,
                QStringLiteral("AppX / MSIX 来源未完整枚举；已观察到的证据仍保留")
            });
        }
    }
    return application;
}

ApplicationInfo unknownApplicationInfo(const QString &scope, const QFileInfo &directory)
{
    ApplicationInfo application;
    application.id = targetId(scope, directory.absoluteFilePath());
    application.name = directory.fileName();
    application.publisher = QStringLiteral("未知");
    application.category = QStringLiteral("未识别");
    application.location = QDir::toNativeSeparators(directory.absoluteFilePath());
    application.installState = InstallState::Unknown;
    application.confidence = 20;
    application.risk = RiskLevel::Unknown;
    application.summary = QStringLiteral("尚未获得足够的应用归属证据，所有数据保持 Unknown。");
    application.evidence.append({EvidenceSource::Folder, EvidenceStatus::Partial,
                                 QStringLiteral("仅获得目录名称证据")});
    application.evidence.append({EvidenceSource::Rule, EvidenceStatus::Unavailable,
                                 QStringLiteral("没有可验证的应用规则，不能安全关联安装记录")});
    return application;
}

} // namespace

AppResolver::AppResolver()
    : AppResolver(rules::RuleCatalog::builtIn(), {})
{
}

AppResolver::AppResolver(InstallationEvidenceSnapshot evidence)
    : AppResolver(rules::RuleCatalog::builtIn(), std::move(evidence))
{
}

AppResolver::AppResolver(rules::RuleCatalog catalog)
    : AppResolver(std::move(catalog), {})
{
}

AppResolver::AppResolver(rules::RuleCatalog catalog,
                         InstallationEvidenceSnapshot evidence)
    : m_catalog(std::move(catalog)),
      m_evidence(std::move(evidence))
{
}

QVector<ScanTarget> AppResolver::discoverTargets(const QStringList &roots) const
{
    QVector<ScanTarget> targets;
    for (const QString &root : roots) {
        const QString scope = rootScopeName(root);
        const std::optional<RuleScope> ruleScope = rootRuleScope(root);
        const QDir rootDirectory(root);
        if (!rootDirectory.exists())
            continue;

        QSet<QString> knownPaths;
        if (ruleScope) {
            for (const ApplicationRule &rule : m_catalog.applications()) {
                for (const RuleLocation &location : rule.locations) {
                    if (*ruleScope != location.scope)
                        continue;

                    const QString path = QDir::cleanPath(
                            rootDirectory.filePath(location.relativePath));
                    if (!QFileInfo(path).isDir())
                        continue;

                    targets.append({
                        knownApplicationInfo(rule, location, path, m_evidence),
                        path,
                        {},
                        rule.entries,
                        ruleSource(rule)
                    });
                    knownPaths.insert(normalizedAbsolutePath(path));
                }
            }
        }

        const QFileInfoList directories = rootDirectory.entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &directory : directories) {
            const QString path = QDir::cleanPath(directory.absoluteFilePath());
            const QString normalizedPath = normalizedAbsolutePath(path);
            if (knownPaths.contains(normalizedPath))
                continue;

            QStringList exclusions;
            const QString descendantPrefix = normalizedPath + QLatin1Char('/');
            for (const QString &knownPath : std::as_const(knownPaths)) {
                if (knownPath.startsWith(descendantPrefix))
                    exclusions.append(QDir::toNativeSeparators(knownPath));
            }
            exclusions.sort(Qt::CaseInsensitive);
            targets.append({unknownApplicationInfo(scope, directory), path, exclusions, {}, {}});
        }
    }

    return targets;
}

} // namespace wam::core
