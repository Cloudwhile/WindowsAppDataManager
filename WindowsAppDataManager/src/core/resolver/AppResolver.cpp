#include "AppResolver.h"
#include "../rules/IdentifierNormalization.h"
#include "../rules/RulePathResolver.h"
#include "../../platform/windows/filesystem/PathPresenceReader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QHash>
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

QString normalizedScanPathKey(const QString &path)
{
    if (path.isEmpty())
        return {};

    QString normalized = QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (normalized.isEmpty() || !QDir::isAbsolutePath(normalized))
        return {};
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
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

    QVector<InstallationCandidate> completeMatches;
    QVector<InstallationCandidate> missingPublisherMatches;
    QVector<InstallationCandidate> conflictingPublisherMatches;
    for (const InstallationCandidate &candidate : std::as_const(candidates)) {
        const bool identityMatches = matchesAnyIdentity(candidate.primary, identities);
        if (!identityMatches)
            continue;
        if (publishers.isEmpty()) {
            completeMatches.append(candidate);
            continue;
        }
        if (candidate.publisher.trimmed().isEmpty()) {
            missingPublisherMatches.append(candidate);
            continue;
        }
        if (matchesAnyIdentity(candidate.publisher, publishers))
            completeMatches.append(candidate);
        else
            conflictingPublisherMatches.append(candidate);
    }

    const bool incomplete = availability != InstallationEvidenceAvailability::Complete
            || !missingPublisherMatches.isEmpty();
    if (completeMatches.size() == 1) {
        return {InstallationMatchStatus::Matched, completeMatches.constFirst(), 1,
                incomplete};
    }
    if (completeMatches.size() > 1) {
        return {InstallationMatchStatus::Ambiguous, completeMatches.constFirst(),
                static_cast<int>(completeMatches.size()), incomplete};
    }
    if (!missingPublisherMatches.isEmpty()) {
        return {InstallationMatchStatus::Incomplete,
                missingPublisherMatches.constFirst(),
                static_cast<int>(missingPublisherMatches.size()), true};
    }
    if (!conflictingPublisherMatches.isEmpty()) {
        if (availability != InstallationEvidenceAvailability::Complete)
            return {InstallationMatchStatus::Incomplete, {}, 0, true};
        return {InstallationMatchStatus::Conflict,
                conflictingPublisherMatches.constFirst(),
                static_cast<int>(conflictingPublisherMatches.size()), incomplete};
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
        return match.candidateCount > 0
                ? QStringLiteral("注册表显示名称已命中，但 %1 条记录缺少规则要求的发布者")
                          .arg(match.candidateCount)
                : QStringLiteral("注册表安装信息仅部分可用，未获得可用于否定安装状态的完整证据");
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
        return match.candidateCount > 0
                ? QStringLiteral("AppX / MSIX 包名已命中，但 %1 条记录缺少规则要求的发布者")
                          .arg(match.candidateCount)
                : QStringLiteral("AppX / MSIX 包信息仅部分可用，未获得可用于否定安装状态的完整证据");
    case InstallationMatchStatus::Unavailable:
        return QStringLiteral("AppX / MSIX 包信息当前不可用，未降低目录归属置信度");
    case InstallationMatchStatus::NotConfigured:
        return {};
    }
    return {};
}

struct ExecutableMatch {
    EvidenceStatus status = EvidenceStatus::Unavailable;
    QString detail;
    std::optional<EvidenceInfo> publisherEvidence;
    bool positive = false;
    bool metadataMatched = false;
    bool signatureMatched = false;
    bool signatureUntrusted = false;
    bool conflict = false;
};

struct RunningProcessMatch {
    EvidenceStatus status = EvidenceStatus::Unavailable;
    QString detail;
};

struct InstallationPathMatch {
    EvidenceStatus status = EvidenceStatus::Unavailable;
    QString detail;
};

bool hasMetadataIdentifiers(const RuleIdentifiers &identifiers)
{
    return !identifiers.executableProductNames.isEmpty()
            || !identifiers.executableCompanyNames.isEmpty()
            || !identifiers.executableOriginalFilenames.isEmpty();
}

QString observedMetadataDetail(const ExecutableEvidenceRecord &record)
{
    QStringList values;
    if (!record.productName.isEmpty())
        values.append(QStringLiteral("产品“%1”").arg(record.productName));
    if (!record.companyName.isEmpty())
        values.append(QStringLiteral("公司“%1”").arg(record.companyName));
    if (!record.originalFilename.isEmpty())
        values.append(QStringLiteral("原始文件“%1”").arg(record.originalFilename));
    return values.join(QStringLiteral("，"));
}

QStringList metadataMismatches(const RuleIdentifiers &identifiers,
                               const ExecutableEvidenceRecord &record)
{
    QStringList mismatches;
    if (!identifiers.executableProductNames.isEmpty()
            && !record.productName.trimmed().isEmpty()
            && !matchesAnyIdentity(record.productName,
                                   identifiers.executableProductNames)) {
        mismatches.append(QStringLiteral("产品名"));
    }
    if (!identifiers.executableCompanyNames.isEmpty()
            && !record.companyName.trimmed().isEmpty()
            && !matchesAnyIdentity(record.companyName,
                                   identifiers.executableCompanyNames)) {
        mismatches.append(QStringLiteral("公司名"));
    }
    if (!identifiers.executableOriginalFilenames.isEmpty()
            && !record.originalFilename.trimmed().isEmpty()
            && !matchesAnyIdentity(record.originalFilename,
                                   identifiers.executableOriginalFilenames)) {
        mismatches.append(QStringLiteral("原始文件名"));
    }
    return mismatches;
}

QStringList missingMetadataFields(const RuleIdentifiers &identifiers,
                                  const ExecutableEvidenceRecord &record)
{
    QStringList missing;
    if (!identifiers.executableProductNames.isEmpty()
            && record.productName.trimmed().isEmpty()) {
        missing.append(QStringLiteral("产品名"));
    }
    if (!identifiers.executableCompanyNames.isEmpty()
            && record.companyName.trimmed().isEmpty()) {
        missing.append(QStringLiteral("公司名"));
    }
    if (!identifiers.executableOriginalFilenames.isEmpty()
            && record.originalFilename.trimmed().isEmpty()) {
        missing.append(QStringLiteral("原始文件名"));
    }
    return missing;
}

EvidenceInfo publisherEvidence(const RuleIdentifiers &identifiers,
                               const ExecutableEvidenceRecord &record,
                               bool &signatureMatched,
                               bool &signatureConflict)
{
    const bool expected = !identifiers.authenticodePublishers.isEmpty();
    switch (record.authenticodeState) {
    case AuthenticodeState::Trusted: {
        if (record.signerPublisher.trimmed().isEmpty()) {
            return {EvidenceSource::Publisher, EvidenceStatus::Incomplete,
                    QStringLiteral("签名信任验证通过，但无法读取签名发布者名称")};
        }
        const bool matched = expected
                && matchesAnyIdentity(record.signerPublisher,
                                      identifiers.authenticodePublishers);
        signatureMatched = matched;
        signatureConflict = expected && !matched;
        if (matched) {
            return {EvidenceSource::Publisher, EvidenceStatus::Matched,
                    QStringLiteral("可信 Authenticode 签名发布者“%1”与规则精确一致")
                            .arg(record.signerPublisher)};
        }
        if (signatureConflict) {
            return {EvidenceSource::Publisher, EvidenceStatus::Conflict,
                    QStringLiteral("可信签名发布者“%1”不在规则允许列表中")
                            .arg(record.signerPublisher)};
        }
        return {EvidenceSource::Publisher, EvidenceStatus::Partial,
                QStringLiteral("签名可信，发布者为“%1”；规则未声明签名发布者标识")
                        .arg(record.signerPublisher)};
    }
    case AuthenticodeState::Unsigned:
        return {EvidenceSource::Publisher, EvidenceStatus::NotFound,
                expected
                        ? QStringLiteral("文件没有 Authenticode 签名，无法满足规则的发布者验证")
                        : QStringLiteral("文件没有 Authenticode 签名，未获得发布者证据")};
    case AuthenticodeState::Untrusted:
        return {EvidenceSource::Publisher, EvidenceStatus::Incomplete,
                record.signerPublisher.trimmed().isEmpty()
                        ? QStringLiteral("存在 Authenticode 签名，但离线信任验证未通过")
                        : QStringLiteral("存在发布者为“%1”的 Authenticode 签名，但离线信任验证未通过")
                                  .arg(record.signerPublisher)};
    case AuthenticodeState::Unavailable:
        return {EvidenceSource::Publisher, EvidenceStatus::Unavailable,
                QStringLiteral("Authenticode 验证当前不可用，未获得发布者证据")};
    }
    return {EvidenceSource::Publisher, EvidenceStatus::Unavailable,
            QStringLiteral("Authenticode 状态未知")};
}

ExecutableMatch matchExecutableEvidence(
        const ApplicationRule &rule,
        const QString &expectedPath,
        const InstallationEvidenceSourceSnapshot<ExecutableEvidenceRecord> &snapshot)
{
    QVector<const ExecutableEvidenceRecord *> matches;
    const QString expectedKey = rules::normalizedPathKey(expectedPath);
    if (expectedKey.isEmpty()) {
        return {EvidenceStatus::Unavailable,
                QStringLiteral("规则路径依赖的环境变量当前不可用，未访问相对路径")};
    }
    for (const ExecutableEvidenceRecord &record : snapshot.records) {
        const QString recordKey = rules::normalizedPathKey(record.path);
        if (!recordKey.isEmpty() && recordKey == expectedKey)
            matches.append(&record);
    }

    if (matches.size() > 1) {
        return {EvidenceStatus::Ambiguous,
                QStringLiteral("可执行文件证据包含 %1 条相同路径记录，未选择不确定结果")
                        .arg(matches.size())};
    }
    if (matches.isEmpty()) {
        if (snapshot.availability == InstallationEvidenceAvailability::Unavailable) {
            return {EvidenceStatus::Unavailable,
                    QStringLiteral("可执行文件证据当前不可用，未降低目录归属置信度")};
        }
        return {EvidenceStatus::Incomplete,
                QStringLiteral("可执行文件证据快照缺少该规则路径，未作否定判断")};
    }

    const ExecutableEvidenceRecord &record = *matches.constFirst();
    if (record.pathState == ExecutablePathState::Missing) {
        return {EvidenceStatus::NotFound,
                QStringLiteral("未找到规则声明的可执行文件，不能据此判断为残留")};
    }
    if (record.pathState == ExecutablePathState::Unavailable) {
        return {EvidenceStatus::Unavailable,
                QStringLiteral("无法读取规则声明的可执行文件，未作否定判断")};
    }

    ExecutableMatch result;
    const bool metadataExpected = hasMetadataIdentifiers(rule.identifiers);
    if (!metadataExpected) {
        result.status = EvidenceStatus::Partial;
        result.detail = QStringLiteral("预期可执行路径存在；规则未声明版本资源标识");
        result.positive = true;
    } else if (record.metadataState == VersionMetadataState::Available) {
        const QStringList mismatches = metadataMismatches(rule.identifiers, record);
        const QStringList missingFields = missingMetadataFields(rule.identifiers, record);
        if (!mismatches.isEmpty()) {
            result.status = EvidenceStatus::Conflict;
            result.detail = QStringLiteral("预期路径存在，但版本资源中的%1与规则不一致")
                                    .arg(mismatches.join(QStringLiteral("、")));
            result.conflict = true;
        } else if (!missingFields.isEmpty()) {
            result.status = EvidenceStatus::Incomplete;
            result.detail = QStringLiteral("预期路径存在，但版本资源缺少规则要求的%1")
                                    .arg(missingFields.join(QStringLiteral("、")));
        } else {
            result.status = EvidenceStatus::Matched;
            const QString observed = observedMetadataDetail(record);
            result.detail = observed.isEmpty()
                    ? QStringLiteral("可执行文件版本资源与规则精确匹配")
                    : QStringLiteral("可执行文件版本资源精确匹配：%1").arg(observed);
            result.positive = true;
            result.metadataMatched = true;
        }
    } else if (record.metadataState == VersionMetadataState::Missing) {
        result.status = EvidenceStatus::Incomplete;
        result.detail = QStringLiteral("预期路径存在，但文件没有规则要求的版本资源");
    } else {
        result.status = EvidenceStatus::Incomplete;
        result.detail = QStringLiteral("预期路径存在，但版本资源当前无法读取");
    }

    bool signatureConflict = false;
    result.publisherEvidence = publisherEvidence(
            rule.identifiers, record, result.signatureMatched, signatureConflict);
    result.signatureUntrusted = record.authenticodeState == AuthenticodeState::Untrusted;
    result.conflict = result.conflict || signatureConflict;
    if (signatureConflict)
        result.positive = false;

    const bool signatureExpected = !rule.identifiers.authenticodePublishers.isEmpty();
    if (signatureExpected && !result.signatureMatched) {
        result.positive = false;
    } else if (result.signatureUntrusted) {
        result.positive = false;
    }
    return result;
}

RunningProcessMatch matchRunningProcessEvidence(
        const QString &expectedPath,
        const RunningProcessEvidenceSnapshot &snapshot)
{
    const QString expectedKey = rules::normalizedPathKey(expectedPath);
    if (expectedKey.isEmpty()) {
        return {EvidenceStatus::Unavailable,
                QStringLiteral("规则可执行路径当前不可解析，未匹配运行进程")};
    }

    int processCount = 0;
    bool matchingUnreadableProcess = false;
    const QString expectedImageName = QFileInfo(expectedPath).fileName();
    for (const RunningProcessEvidenceRecord &record : snapshot.records) {
        const QString processKey = rules::normalizedPathKey(record.imagePath);
        if (!processKey.isEmpty() && processKey == expectedKey)
            ++processCount;
        else if (processKey.isEmpty()
                 && !expectedImageName.isEmpty()
                 && (record.imageName.trimmed().isEmpty()
                     || record.imageName.compare(
                                expectedImageName, Qt::CaseInsensitive) == 0))
            matchingUnreadableProcess = true;
    }
    if (processCount > 0) {
        return {
            EvidenceStatus::Matched,
            processCount == 1
                    ? QStringLiteral("检测到进程正在从规则声明的可执行路径运行")
                    : QStringLiteral("检测到 %1 个进程正在从规则声明的可执行路径运行")
                              .arg(processCount)
        };
    }

    if (snapshot.availability == InstallationEvidenceAvailability::Unavailable) {
        return {EvidenceStatus::Unavailable,
                QStringLiteral("运行进程证据当前不可用，未作否定判断")};
    }
    if (matchingUnreadableProcess) {
        return {EvidenceStatus::Incomplete,
                QStringLiteral("检测到同名进程，但无法读取其可执行路径，不能确认应用未运行")};
    }
    const bool enumerationComplete = snapshot.enumerationComplete
            || snapshot.availability == InstallationEvidenceAvailability::Complete;
    if (!enumerationComplete) {
        return {EvidenceStatus::Incomplete,
                QStringLiteral("运行进程枚举未完整结束，未观察到匹配进程，不能作否定判断")};
    }
    return {EvidenceStatus::NotFound,
            QStringLiteral("当前未检测到匹配进程；进程未运行不代表应用未安装")};
}

InstallationPathMatch matchInstallPathEvidence(
        const QString &expectedPath,
        const InstallationEvidenceSourceSnapshot<InstallationPathEvidenceRecord> &snapshot)
{
    const QString expectedKey = rules::normalizedPathKey(expectedPath);
    if (expectedKey.isEmpty()) {
        return {EvidenceStatus::Unavailable,
                QStringLiteral("规则安装路径当前不可解析")};
    }

    QVector<const InstallationPathEvidenceRecord *> matches;
    for (const InstallationPathEvidenceRecord &record : snapshot.records) {
        const QString recordKey = rules::normalizedPathKey(record.path);
        if (!recordKey.isEmpty() && recordKey == expectedKey)
            matches.append(&record);
    }
    if (matches.size() > 1) {
        return {EvidenceStatus::Ambiguous,
                QStringLiteral("安装路径证据包含 %1 条相同路径记录")
                        .arg(matches.size())};
    }
    if (matches.isEmpty()) {
        if (snapshot.availability == InstallationEvidenceAvailability::Unavailable) {
            return {EvidenceStatus::Unavailable,
                    QStringLiteral("安装路径证据当前不可用")};
        }
        return {EvidenceStatus::Incomplete,
                QStringLiteral("安装路径证据快照缺少该规则路径")};
    }

    const InstallationPathEvidenceRecord &record = *matches.constFirst();
    switch (record.state) {
    case InstallationPathState::Present:
        return {EvidenceStatus::Matched,
                record.directory
                        ? QStringLiteral("规则声明的安装目录仍然存在")
                        : QStringLiteral("规则声明的安装路径仍然存在，但目标不是目录")};
    case InstallationPathState::Missing:
        return {EvidenceStatus::NotFound,
                QStringLiteral("未找到规则声明的安装路径")};
    case InstallationPathState::Unavailable:
        return {EvidenceStatus::Unavailable,
                QStringLiteral("无法读取规则声明的安装路径状态")};
    }
    return {EvidenceStatus::Unavailable,
            QStringLiteral("安装路径证据状态未知")};
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

QString validatedEvidenceInstallPath(const QString &value)
{
    const QString expanded = rules::expandRulePath(value);
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
    QString stablePath = normalizedScanPathKey(path);
    if (stablePath.isEmpty())
        stablePath = QDir::cleanPath(QDir::fromNativeSeparators(path));
    const QByteArray digest = QCryptographicHash::hash(
            stablePath.toUtf8(), QCryptographicHash::Sha256)
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
            rules::expandRulePath(rule.executablePath));
    application.installPath = QDir::toNativeSeparators(
            rules::expandRulePath(rule.installPath));

    const ExecutableMatch executableMatch = matchExecutableEvidence(
            rule, application.executablePath, evidence.executable);
    const RunningProcessMatch runningProcessMatch = matchRunningProcessEvidence(
            application.executablePath, evidence.runningProcesses);
    const InstallationPathMatch installPathMatch = matchInstallPathEvidence(
            application.installPath, evidence.installPaths);
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
    const int strongEvidenceCount = static_cast<int>(executableMatch.positive)
            + static_cast<int>(registryPositive) + static_cast<int>(appxPositive);
    const bool hasAmbiguity = registryMatch.status == InstallationMatchStatus::Ambiguous
            || appxMatch.status == InstallationMatchStatus::Ambiguous;
    const bool hasConflict = registryMatch.status == InstallationMatchStatus::Conflict
            || appxMatch.status == InstallationMatchStatus::Conflict
            || executableMatch.conflict;

    application.installState = strongEvidenceCount > 0
            ? InstallState::Installed : InstallState::Unknown;
    if (strongEvidenceCount >= 2)
        application.confidence = 98;
    else if (executableMatch.positive && executableMatch.signatureMatched)
        application.confidence = 97;
    else if (appxMatched)
        application.confidence = 96;
    else if (registryMatched)
        application.confidence = 94;
    else if (executableMatch.positive)
        application.confidence = 88;
    else if (registryPositive || appxPositive)
        application.confidence = 90;
    else
        application.confidence = 72;
    if (hasAmbiguity)
        application.confidence = std::min(application.confidence, 96);
    if (executableMatch.signatureUntrusted)
        application.confidence = std::min(application.confidence, 90);
    if (hasConflict) {
        application.confidence = strongEvidenceCount == 0
                ? 49 : std::min(application.confidence, 90);
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
        executableMatch.status,
        executableMatch.detail
    });
    if (executableMatch.publisherEvidence)
        application.evidence.append(*executableMatch.publisherEvidence);
    application.evidence.append({
        EvidenceSource::RunningProcess,
        runningProcessMatch.status,
        runningProcessMatch.detail
    });
    application.evidence.append({
        EvidenceSource::InstallPath,
        installPathMatch.status,
        installPathMatch.detail
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
    QHash<QString, bool> locationDiscoveryComplete;
    QHash<QString, QStringList> locationDiscoveryIssues;
    QSet<int> suppliedScopes;
    for (const QString &root : roots) {
        const auto scope = rootRuleScope(root);
        if (scope)
            suppliedScopes.insert(static_cast<int>(*scope));
    }
    for (const ApplicationRule &rule : m_catalog.applications()) {
        locationDiscoveryComplete.insert(rule.id, true);
        for (const RuleLocation &location : rule.locations) {
            if (suppliedScopes.contains(static_cast<int>(location.scope)))
                continue;
            locationDiscoveryComplete[rule.id] = false;
            locationDiscoveryIssues[rule.id].append(
                    QStringLiteral("未提供 %1 AppData 扫描根目录")
                            .arg(scopeName(location.scope)));
        }
    }

    for (const QString &root : roots) {
        const QString scope = rootScopeName(root);
        const std::optional<RuleScope> ruleScope = rootRuleScope(root);
        const QDir rootDirectory(root);
        const platform::windows::PathPresenceResult rootPresence =
                platform::windows::PathPresenceReader::read(root);
        if (rootPresence.state != platform::windows::PathPresenceState::Present
                || !rootPresence.directory) {
            if (ruleScope
                    && rootPresence.state != platform::windows::PathPresenceState::Missing) {
                for (const ApplicationRule &rule : m_catalog.applications()) {
                    const bool usesScope = std::any_of(
                            rule.locations.cbegin(), rule.locations.cend(),
                            [ruleScope](const RuleLocation &location) {
                        return location.scope == *ruleScope;
                    });
                    if (!usesScope)
                        continue;
                    locationDiscoveryComplete[rule.id] = false;
                    locationDiscoveryIssues[rule.id].append(
                            QStringLiteral("无法确认 %1 AppData 根目录状态：%2")
                                    .arg(scope, QDir::toNativeSeparators(root)));
                }
            }
            continue;
        }

        QSet<QString> knownPaths;
        if (ruleScope) {
            for (const ApplicationRule &rule : m_catalog.applications()) {
                for (const RuleLocation &location : rule.locations) {
                    if (*ruleScope != location.scope)
                        continue;

                    const QString path = QDir::cleanPath(
                            rootDirectory.filePath(location.relativePath));
                    const platform::windows::PathPresenceResult presence =
                            platform::windows::PathPresenceReader::read(path);
                    if (presence.state == platform::windows::PathPresenceState::Missing)
                        continue;
                    if (presence.state != platform::windows::PathPresenceState::Present
                            || !presence.directory) {
                        locationDiscoveryComplete[rule.id] = false;
                        locationDiscoveryIssues[rule.id].append(
                                presence.state == platform::windows::PathPresenceState::Present
                                        ? QStringLiteral("规则数据位置存在，但目标不是目录：%1")
                                                  .arg(QDir::toNativeSeparators(path))
                                        : QStringLiteral("无法确认规则数据位置状态：%1")
                                                  .arg(QDir::toNativeSeparators(path)));
                        continue;
                    }

                    targets.append({
                        knownApplicationInfo(rule, location, path, m_evidence),
                        path,
                        {},
                        rule.entries,
                        ruleSource(rule),
                        location.ownership,
                        true
                    });
                    const QString knownPath = normalizedScanPathKey(path);
                    if (!knownPath.isEmpty())
                        knownPaths.insert(knownPath);
                }
            }
        }

        const QFileInfoList directories = rootDirectory.entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &directory : directories) {
            const QString path = QDir::cleanPath(directory.absoluteFilePath());
            const QString normalizedPath = normalizedScanPathKey(path);
            if (!normalizedPath.isEmpty() && knownPaths.contains(normalizedPath))
                continue;

            QStringList exclusions;
            if (!normalizedPath.isEmpty()) {
                const QString descendantPrefix = normalizedPath + QLatin1Char('/');
                for (const QString &knownPath : std::as_const(knownPaths)) {
                    if (knownPath.startsWith(descendantPrefix))
                        exclusions.append(QDir::toNativeSeparators(knownPath));
                }
            }
            exclusions.sort(Qt::CaseInsensitive);
            targets.append({unknownApplicationInfo(scope, directory), path, exclusions, {}, {},
                            RuleLocationOwnership::Shared, true});
        }
    }

    for (ScanTarget &target : targets) {
        const auto complete = locationDiscoveryComplete.constFind(target.application.id);
        if (complete == locationDiscoveryComplete.cend())
            continue;
        target.locationDiscoveryComplete = *complete;
        for (const QString &detail : locationDiscoveryIssues.value(target.application.id)) {
            target.application.evidence.append({
                EvidenceSource::Folder,
                EvidenceStatus::Unavailable,
                detail
            });
        }
    }

    return targets;
}

} // namespace wam::core
