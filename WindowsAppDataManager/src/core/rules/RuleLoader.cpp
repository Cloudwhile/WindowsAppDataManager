#include "RuleLoader.h"
#include "GlobMatcher.h"
#include "IdentifierNormalization.h"
#include "RulePathResolver.h"

#include <QDir>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <utility>

namespace wam::core::rules {
namespace {

void addIssue(QVector<RuleLoadIssue> &issues,
              RuleIssueCode code,
              const QString &source,
              const QString &field,
              const QString &message)
{
    issues.append({code, source, field, message});
}

RuleOrigin originForSource(const QString &source)
{
    if (source.startsWith(QStringLiteral(":/"))
            || source.startsWith(QStringLiteral("qrc:/"), Qt::CaseInsensitive)) {
        return RuleOrigin::BuiltIn;
    }

    const QString normalized = QDir::fromNativeSeparators(source).toCaseFolded();
    if (normalized.contains(QStringLiteral("community")))
        return RuleOrigin::Community;
    return RuleOrigin::Local;
}

RuleTrustLevel trustLevelForOrigin(RuleOrigin origin)
{
    return origin == RuleOrigin::BuiltIn
            ? RuleTrustLevel::Verified : RuleTrustLevel::Unverified;
}

void rejectUnknownFields(const QJsonObject &object,
                         const QSet<QString> &allowedFields,
                         const QString &prefix,
                         const QString &source,
                         QVector<RuleLoadIssue> &issues)
{
    for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
        if (allowedFields.contains(iterator.key()))
            continue;
        const QString field = prefix.isEmpty()
                ? iterator.key() : prefix + QLatin1Char('.') + iterator.key();
        addIssue(issues, RuleIssueCode::InvalidValue, source, field,
                 QStringLiteral("包含不受支持的字段"));
    }
}

std::optional<QString> requiredString(const QJsonObject &object,
                                      const QString &key,
                                      const QString &field,
                                      const QString &source,
                                      QVector<RuleLoadIssue> &issues)
{
    if (!object.contains(key)) {
        addIssue(issues, RuleIssueCode::MissingField, source, field,
                 QStringLiteral("缺少必填字段"));
        return std::nullopt;
    }

    const QJsonValue value = object.value(key);
    if (!value.isString()) {
        addIssue(issues, RuleIssueCode::InvalidType, source, field,
                 QStringLiteral("字段必须是字符串"));
        return std::nullopt;
    }

    const QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        addIssue(issues, RuleIssueCode::InvalidValue, source, field,
                 QStringLiteral("字段不能为空"));
        return std::nullopt;
    }
    return text;
}

std::optional<QString> requiredPathString(const QJsonObject &object,
                                          const QString &key,
                                          const QString &field,
                                          const QString &source,
                                          QVector<RuleLoadIssue> &issues)
{
    if (!object.contains(key)) {
        addIssue(issues, RuleIssueCode::MissingField, source, field,
                 QStringLiteral("缺少必填字段"));
        return std::nullopt;
    }

    const QJsonValue value = object.value(key);
    if (!value.isString()) {
        addIssue(issues, RuleIssueCode::InvalidType, source, field,
                 QStringLiteral("字段必须是字符串"));
        return std::nullopt;
    }

    const QString text = value.toString();
    if (text.trimmed().isEmpty()) {
        addIssue(issues, RuleIssueCode::InvalidValue, source, field,
                 QStringLiteral("字段不能为空"));
        return std::nullopt;
    }
    return text;
}

bool hasSafeIdentifier(const QString &identifier)
{
    static const QRegularExpression pattern(
            QStringLiteral("^[a-z0-9][a-z0-9._-]*$"));
    return pattern.match(identifier).hasMatch();
}

std::optional<QString> normalizedRelativePath(const QString &rawPath,
                                              bool allowRoot,
                                              const QString &field,
                                              const QString &source,
                                              QVector<RuleLoadIssue> &issues,
                                              bool allowGlob = false)
{
    QString error;
    if (!validateRelativeRulePath(rawPath, allowRoot, &error, allowGlob)) {
        addIssue(issues, RuleIssueCode::UnsafePath, source, field,
                 QStringLiteral("规则相对路径不安全：%1").arg(error));
        return std::nullopt;
    }

    return QDir::fromNativeSeparators(rawPath);
}

bool pathPrefixesOverlap(const QString &left, const QString &right)
{
    const QString leftKey = left.toCaseFolded();
    const QString rightKey = right.toCaseFolded();
    return leftKey == rightKey
            || leftKey.startsWith(rightKey + QLatin1Char('/'))
            || rightKey.startsWith(leftKey + QLatin1Char('/'));
}

std::optional<RuleScope> parseScope(const QString &value)
{
    const QString normalized = value.toCaseFolded();
    if (normalized == QStringLiteral("local"))
        return RuleScope::Local;
    if (normalized == QStringLiteral("roaming"))
        return RuleScope::Roaming;
    if (normalized == QStringLiteral("locallow"))
        return RuleScope::LocalLow;
    return std::nullopt;
}

std::optional<RuleLocationOwnership> parseLocationOwnership(const QString &value)
{
    const QString normalized = value.toCaseFolded();
    if (normalized == QStringLiteral("shared"))
        return RuleLocationOwnership::Shared;
    if (normalized == QStringLiteral("exclusive"))
        return RuleLocationOwnership::Exclusive;
    return std::nullopt;
}

std::optional<RuleLocationRole> parseLocationRole(const QString &value)
{
    static const QHash<QString, RuleLocationRole> roles {
        {QStringLiteral("data"), RuleLocationRole::Data},
        {QStringLiteral("config"), RuleLocationRole::Config},
        {QStringLiteral("cache"), RuleLocationRole::Cache},
        {QStringLiteral("shared-data"), RuleLocationRole::SharedData},
        {QStringLiteral("install-payload"), RuleLocationRole::InstallPayload},
        {QStringLiteral("vendor-namespace"), RuleLocationRole::VendorNamespace},
        {QStringLiteral("mixed"), RuleLocationRole::Mixed}
    };
    const auto iterator = roles.constFind(value.toCaseFolded());
    return iterator == roles.cend()
            ? std::nullopt : std::optional<RuleLocationRole>(*iterator);
}

bool parsePathAlternatives(const QJsonObject &object,
                           const QString &singularKey,
                           const QString &pluralKey,
                           const QString &field,
                           const QString &source,
                           QVector<RuleLoadIssue> &issues,
                           QStringList &paths)
{
    const bool hasSingular = object.contains(singularKey);
    const bool hasPlural = object.contains(pluralKey);
    if (!hasSingular && !hasPlural) {
        addIssue(issues, RuleIssueCode::MissingField, source, field,
                 QStringLiteral("缺少必填字段"));
        return false;
    }

    auto appendPath = [&](const QString &value, const QString &pathField) {
        if (value.trimmed().isEmpty()) {
            addIssue(issues, RuleIssueCode::InvalidValue, source, pathField,
                     QStringLiteral("路径不能为空"));
            return;
        }
        const QString normalized = QDir::fromNativeSeparators(value.trimmed())
                .toCaseFolded();
        const bool duplicate = std::any_of(
                paths.cbegin(), paths.cend(), [&normalized](const QString &existing) {
            return QDir::fromNativeSeparators(existing.trimmed()).toCaseFolded()
                    == normalized;
        });
        if (duplicate) {
            addIssue(issues, RuleIssueCode::InvalidValue, source, pathField,
                     QStringLiteral("路径候选不能重复"));
            return;
        }
        paths.append(value);
    };

    if (hasSingular) {
        const QJsonValue value = object.value(singularKey);
        if (!value.isString()) {
            addIssue(issues, RuleIssueCode::InvalidType, source, field,
                     QStringLiteral("字段必须是字符串"));
        } else {
            appendPath(value.toString(), field);
        }
    }
    if (hasPlural) {
        const QString arrayField = QStringLiteral("%1").arg(pluralKey);
        const QJsonValue value = object.value(pluralKey);
        if (!value.isArray()) {
            addIssue(issues, RuleIssueCode::InvalidType, source, arrayField,
                     QStringLiteral("路径候选必须是字符串数组"));
        } else {
            const QJsonArray array = value.toArray();
            if (array.isEmpty()) {
                addIssue(issues, RuleIssueCode::InvalidValue, source, arrayField,
                         QStringLiteral("路径候选不能为空"));
            }
            for (qsizetype index = 0; index < array.size(); ++index) {
                const QString itemField = QStringLiteral("%1[%2]")
                        .arg(arrayField).arg(index);
                const QJsonValue item = array.at(index);
                if (item.isString()) {
                    appendPath(item.toString(), itemField);
                    continue;
                }
                if (pluralKey == QStringLiteral("executables") && item.isObject()) {
                    const QJsonObject executable = item.toObject();
                    rejectUnknownFields(executable, {QStringLiteral("path")},
                                        itemField, source, issues);
                    const auto path = requiredPathString(
                            executable, QStringLiteral("path"),
                            itemField + QStringLiteral(".path"), source, issues);
                    if (path)
                        appendPath(*path, itemField + QStringLiteral(".path"));
                    continue;
                }
                addIssue(issues, RuleIssueCode::InvalidType, source, itemField,
                         pluralKey == QStringLiteral("executables")
                                 ? QStringLiteral("可执行路径候选必须是字符串或包含 path 的对象")
                                 : QStringLiteral("路径候选必须是字符串"));
            }
        }
    }
    return !paths.isEmpty();
}

std::optional<DataCategory> parseCategory(const QString &value)
{
    static const QHash<QString, DataCategory> categories {
        {QStringLiteral("unknown"), DataCategory::Unknown},
        {QStringLiteral("cache"), DataCategory::Cache},
        {QStringLiteral("log"), DataCategory::Log},
        {QStringLiteral("temp"), DataCategory::Temp},
        {QStringLiteral("crash-dump"), DataCategory::CrashDump},
        {QStringLiteral("config"), DataCategory::Config},
        {QStringLiteral("database"), DataCategory::Database},
        {QStringLiteral("session"), DataCategory::Session},
        {QStringLiteral("cookie"), DataCategory::Cookie},
        {QStringLiteral("credential"), DataCategory::Credential},
        {QStringLiteral("user-data"), DataCategory::UserData},
        {QStringLiteral("workspace"), DataCategory::Workspace},
        {QStringLiteral("save-game"), DataCategory::SaveGame},
        {QStringLiteral("downloaded-resource"), DataCategory::DownloadedResource},
        {QStringLiteral("extension"), DataCategory::Extension}
    };
    const auto iterator = categories.constFind(value.toCaseFolded());
    return iterator == categories.cend()
            ? std::nullopt : std::optional<DataCategory>(*iterator);
}

std::optional<RiskLevel> parseRisk(const QString &value)
{
    static const QHash<QString, RiskLevel> risks {
        {QStringLiteral("safe"), RiskLevel::Safe},
        {QStringLiteral("low"), RiskLevel::Low},
        {QStringLiteral("caution"), RiskLevel::Caution},
        {QStringLiteral("high"), RiskLevel::High},
        {QStringLiteral("protected"), RiskLevel::Protected},
        {QStringLiteral("unknown"), RiskLevel::Unknown}
    };
    const auto iterator = risks.constFind(value.toCaseFolded());
    return iterator == risks.cend()
            ? std::nullopt : std::optional<RiskLevel>(*iterator);
}

std::optional<RebuildableState> parseRebuildable(const QJsonValue &value)
{
    if (value.isBool())
        return value.toBool() ? RebuildableState::Yes : RebuildableState::No;
    if (value.isString() && value.toString().compare(
                QStringLiteral("unknown"), Qt::CaseInsensitive) == 0) {
        return RebuildableState::Unknown;
    }
    return std::nullopt;
}

void parseIdentifierArray(const QJsonObject &identifiersObject,
                          const QString &key,
                          const QString &source,
                          QStringList &identifiers,
                          QVector<RuleLoadIssue> &issues)
{
    if (!identifiersObject.contains(key))
        return;

    const QString field = QStringLiteral("identifiers.") + key;
    const QJsonValue value = identifiersObject.value(key);
    if (!value.isArray()) {
        addIssue(issues, RuleIssueCode::InvalidType, source, field,
                 QStringLiteral("标识集合必须是字符串数组"));
        return;
    }

    const QJsonArray array = value.toArray();
    if (array.isEmpty()) {
        addIssue(issues, RuleIssueCode::InvalidValue, source, field,
                 QStringLiteral("标识集合不能为空"));
        return;
    }

    QSet<QString> seenValues;
    for (qsizetype index = 0; index < array.size(); ++index) {
        const QString itemField = QStringLiteral("%1[%2]").arg(field).arg(index);
        const QJsonValue item = array.at(index);
        if (!item.isString()) {
            addIssue(issues, RuleIssueCode::InvalidType, source, itemField,
                     QStringLiteral("标识必须是字符串"));
            continue;
        }

        const QString identifier = item.toString().trimmed();
        if (identifier.isEmpty()) {
            addIssue(issues, RuleIssueCode::InvalidValue, source, itemField,
                     QStringLiteral("标识不能为空或仅包含空白"));
            continue;
        }

        const QString canonicalIdentifier = normalizedIdentifier(identifier);
        if (seenValues.contains(canonicalIdentifier)) {
            addIssue(issues, RuleIssueCode::InvalidValue, source, itemField,
                     QStringLiteral("标识集合包含大小写不敏感的重复项"));
            continue;
        }

        seenValues.insert(canonicalIdentifier);
        identifiers.append(identifier);
    }
}

void validateRunningProcessNames(const QString &source,
                                 const QStringList &processNames,
                                 QVector<RuleLoadIssue> &issues)
{
    for (qsizetype index = 0; index < processNames.size(); ++index) {
        const QString &processName = processNames.at(index);
        QString pathError;
        const bool singleSegment = !processName.contains(QLatin1Char('/'))
                && !processName.contains(QLatin1Char('\\'));
        if (singleSegment
                && processName.endsWith(QStringLiteral(".exe"),
                                        Qt::CaseInsensitive)
                && validateRelativeRulePath(processName, false, &pathError)) {
            continue;
        }

        addIssue(issues, RuleIssueCode::InvalidValue, source,
                 QStringLiteral("identifiers.runningProcessNames[%1]").arg(index),
                 QStringLiteral("运行进程名必须是安全的 .exe 文件名，不能包含路径"));
    }
}

void parseIdentifiers(const QJsonObject &root,
                      const QString &source,
                      RuleIdentifiers &identifiers,
                      QVector<RuleLoadIssue> &issues)
{
    const QJsonValue value = root.value(QStringLiteral("identifiers"));
    if (value.isUndefined())
        return;
    if (!value.isObject()) {
        addIssue(issues, RuleIssueCode::InvalidType, source,
                 QStringLiteral("identifiers"),
                 QStringLiteral("字段必须是对象"));
        return;
    }

    const QJsonObject object = value.toObject();
    rejectUnknownFields(object,
                        {QStringLiteral("registryDisplayNames"),
                         QStringLiteral("registryPublishers"),
                         QStringLiteral("appxPackageNames"),
                         QStringLiteral("appxPublishers"),
                         QStringLiteral("executableProductNames"),
                         QStringLiteral("executableCompanyNames"),
                         QStringLiteral("executableOriginalFilenames"),
                         QStringLiteral("authenticodePublishers"),
                         QStringLiteral("runningProcessNames")},
                        QStringLiteral("identifiers"), source, issues);
    if (object.isEmpty()) {
        addIssue(issues, RuleIssueCode::InvalidValue, source,
                 QStringLiteral("identifiers"),
                 QStringLiteral("至少需要提供一种安装标识"));
        return;
    }

    parseIdentifierArray(object, QStringLiteral("registryDisplayNames"), source,
                         identifiers.registryDisplayNames, issues);
    parseIdentifierArray(object, QStringLiteral("registryPublishers"), source,
                         identifiers.registryPublishers, issues);
    parseIdentifierArray(object, QStringLiteral("appxPackageNames"), source,
                         identifiers.appxPackageNames, issues);
    parseIdentifierArray(object, QStringLiteral("appxPublishers"), source,
                         identifiers.appxPublishers, issues);
    parseIdentifierArray(object, QStringLiteral("executableProductNames"), source,
                         identifiers.executableProductNames, issues);
    parseIdentifierArray(object, QStringLiteral("executableCompanyNames"), source,
                         identifiers.executableCompanyNames, issues);
    parseIdentifierArray(object, QStringLiteral("executableOriginalFilenames"), source,
                         identifiers.executableOriginalFilenames, issues);
    parseIdentifierArray(object, QStringLiteral("authenticodePublishers"), source,
                         identifiers.authenticodePublishers, issues);
    parseIdentifierArray(object, QStringLiteral("runningProcessNames"), source,
                         identifiers.runningProcessNames, issues);
    validateRunningProcessNames(source, identifiers.runningProcessNames, issues);

    if (!identifiers.registryPublishers.isEmpty()
            && identifiers.registryDisplayNames.isEmpty()) {
        addIssue(issues, RuleIssueCode::InvalidValue, source,
                 QStringLiteral("identifiers.registryPublishers"),
                 QStringLiteral("注册表发布者只能限定 registryDisplayNames，不能单独标识应用"));
    }
    if (!identifiers.appxPublishers.isEmpty()
            && identifiers.appxPackageNames.isEmpty()) {
        addIssue(issues, RuleIssueCode::InvalidValue, source,
                 QStringLiteral("identifiers.appxPublishers"),
                 QStringLiteral("AppX 发布者只能限定 appxPackageNames，不能单独标识应用"));
    }
}

void parseLocations(const QJsonObject &root,
                    const QString &source,
                    QVector<RuleLocation> &locations,
                    QVector<RuleLoadIssue> &issues)
{
    const QJsonValue value = root.value(QStringLiteral("locations"));
    if (value.isUndefined()) {
        addIssue(issues, RuleIssueCode::MissingField, source,
                 QStringLiteral("locations"), QStringLiteral("缺少必填字段"));
        return;
    }
    if (!value.isArray()) {
        addIssue(issues, RuleIssueCode::InvalidType, source,
                 QStringLiteral("locations"), QStringLiteral("字段必须是数组"));
        return;
    }

    const QJsonArray array = value.toArray();
    if (array.isEmpty()) {
        addIssue(issues, RuleIssueCode::InvalidValue, source,
                 QStringLiteral("locations"), QStringLiteral("至少需要一个应用目录"));
        return;
    }

    QVector<QPair<RuleScope, QString>> seenLocations;
    for (qsizetype index = 0; index < array.size(); ++index) {
        const QString prefix = QStringLiteral("locations[%1]").arg(index);
        if (!array.at(index).isObject()) {
            addIssue(issues, RuleIssueCode::InvalidType, source, prefix,
                     QStringLiteral("目录项必须是对象"));
            continue;
        }

        const QJsonObject locationObject = array.at(index).toObject();
        rejectUnknownFields(locationObject,
                            {QStringLiteral("scope"), QStringLiteral("path"),
                             QStringLiteral("ownership"), QStringLiteral("role")},
                            prefix, source, issues);
        const auto scopeText = requiredString(locationObject, QStringLiteral("scope"),
                                               prefix + QStringLiteral(".scope"),
                                               source, issues);
        const auto pathText = requiredPathString(
                locationObject, QStringLiteral("path"),
                prefix + QStringLiteral(".path"), source, issues);
        if (!scopeText || !pathText)
            continue;

        RuleLocationOwnership ownership = RuleLocationOwnership::Shared;
        if (locationObject.contains(QStringLiteral("ownership"))) {
            const auto ownershipText = requiredString(
                    locationObject, QStringLiteral("ownership"),
                    prefix + QStringLiteral(".ownership"), source, issues);
            if (!ownershipText)
                continue;
            const auto parsedOwnership = parseLocationOwnership(*ownershipText);
            if (!parsedOwnership) {
                addIssue(issues, RuleIssueCode::InvalidValue, source,
                         prefix + QStringLiteral(".ownership"),
                         QStringLiteral("未知目录所有权，只允许 shared 或 exclusive"));
                continue;
            }
            ownership = *parsedOwnership;
        }

        RuleLocationRole role = RuleLocationRole::Data;
        if (locationObject.contains(QStringLiteral("role"))) {
            const auto roleText = requiredString(
                    locationObject, QStringLiteral("role"),
                    prefix + QStringLiteral(".role"), source, issues);
            if (!roleText)
                continue;
            const auto parsedRole = parseLocationRole(*roleText);
            if (!parsedRole) {
                addIssue(issues, RuleIssueCode::InvalidValue, source,
                         prefix + QStringLiteral(".role"),
                         QStringLiteral("未知目录角色"));
                continue;
            }
            role = *parsedRole;
        }

        const auto scope = parseScope(*scopeText);
        if (!scope) {
            addIssue(issues, RuleIssueCode::InvalidValue, source,
                     prefix + QStringLiteral(".scope"),
                     QStringLiteral("未知目录范围，只允许 local、roaming 或 locallow"));
            continue;
        }
        const auto path = normalizedRelativePath(*pathText, false,
                                                 prefix + QStringLiteral(".path"),
                                                 source, issues);
        if (!path)
            continue;

        const bool overlaps = std::any_of(
                seenLocations.cbegin(), seenLocations.cend(),
                [scope, &path](const auto &existing) {
            return existing.first == *scope
                    && pathPrefixesOverlap(existing.second, *path);
        });
        if (overlaps) {
            addIssue(issues, RuleIssueCode::InvalidValue, source, prefix,
                     QStringLiteral("同一 AppData 范围内的应用目录不能重复或互相嵌套"));
            continue;
        }
        seenLocations.append({*scope, *path});
        locations.append({*scope, *path, ownership, role});
    }
}

void parseEntries(const QJsonObject &root,
                  const QString &source,
                  QVector<RuleEntry> &entries,
                  QVector<RuleLoadIssue> &issues)
{
    const QJsonValue value = root.value(QStringLiteral("entries"));
    if (value.isUndefined()) {
        addIssue(issues, RuleIssueCode::MissingField, source,
                 QStringLiteral("entries"), QStringLiteral("缺少必填字段"));
        return;
    }
    if (!value.isArray()) {
        addIssue(issues, RuleIssueCode::InvalidType, source,
                 QStringLiteral("entries"), QStringLiteral("字段必须是数组"));
        return;
    }

    const QJsonArray array = value.toArray();
    if (array.isEmpty()) {
        addIssue(issues, RuleIssueCode::InvalidValue, source,
                 QStringLiteral("entries"), QStringLiteral("至少需要一条数据分类规则"));
        return;
    }

    QSet<QString> seenIds;
    QSet<QString> seenPaths;
    for (qsizetype index = 0; index < array.size(); ++index) {
        const QString prefix = QStringLiteral("entries[%1]").arg(index);
        if (!array.at(index).isObject()) {
            addIssue(issues, RuleIssueCode::InvalidType, source, prefix,
                     QStringLiteral("分类项必须是对象"));
            continue;
        }

        const QJsonObject entryObject = array.at(index).toObject();
        rejectUnknownFields(entryObject,
                            {QStringLiteral("id"), QStringLiteral("path"),
                             QStringLiteral("paths"),
                             QStringLiteral("category"), QStringLiteral("risk"),
                             QStringLiteral("rebuildable"), QStringLiteral("impact")},
                            prefix, source, issues);
        const auto id = requiredString(entryObject, QStringLiteral("id"),
                                       prefix + QStringLiteral(".id"), source, issues);
        QStringList pathTexts;
        const bool hasPaths = parsePathAlternatives(
                entryObject, QStringLiteral("path"), QStringLiteral("paths"),
                prefix + QStringLiteral(".path"), source, issues, pathTexts);
        const auto categoryText = requiredString(entryObject, QStringLiteral("category"),
                                                  prefix + QStringLiteral(".category"),
                                                  source, issues);
        const auto riskText = requiredString(entryObject, QStringLiteral("risk"),
                                              prefix + QStringLiteral(".risk"), source, issues);
        const auto impact = requiredString(entryObject, QStringLiteral("impact"),
                                           prefix + QStringLiteral(".impact"), source, issues);
        if (!entryObject.contains(QStringLiteral("rebuildable"))) {
            addIssue(issues, RuleIssueCode::MissingField, source,
                     prefix + QStringLiteral(".rebuildable"),
                     QStringLiteral("缺少必填字段"));
        }
        if (!id || !hasPaths || !categoryText || !riskText || !impact
                || !entryObject.contains(QStringLiteral("rebuildable"))) {
            continue;
        }

        bool valid = true;
        if (!hasSafeIdentifier(*id)) {
            addIssue(issues, RuleIssueCode::InvalidValue, source,
                     prefix + QStringLiteral(".id"),
                     QStringLiteral("标识只能包含小写字母、数字、点、下划线和连字符"));
            valid = false;
        }
        if (seenIds.contains(*id)) {
            addIssue(issues, RuleIssueCode::DuplicateId, source,
                     prefix + QStringLiteral(".id"), QStringLiteral("分类标识重复"));
            valid = false;
        }

        QStringList normalizedPaths;
        normalizedPaths.reserve(pathTexts.size());
        for (qsizetype pathIndex = 0; pathIndex < pathTexts.size(); ++pathIndex) {
            const QString pathField = pathTexts.size() == 1
                    ? prefix + QStringLiteral(".path")
                    : QStringLiteral("%1.paths[%2]").arg(prefix).arg(pathIndex);
            const QString rawPath = pathTexts.at(pathIndex);
            const bool containsGlob = rawPath.contains(QLatin1Char('*'));
            if (containsGlob) {
                QString globError;
                if (!GlobMatcher::validate(rawPath, &globError)) {
                    addIssue(issues, RuleIssueCode::UnsafePath, source, pathField,
                             QStringLiteral("Glob 路径不安全：%1").arg(globError));
                    valid = false;
                    continue;
                }
            }
            const auto path = normalizedRelativePath(
                    rawPath, !containsGlob, pathField, source, issues, containsGlob);
            if (!path) {
                valid = false;
                continue;
            }
            normalizedPaths.append(*path);
        }
        if (normalizedPaths.isEmpty())
            valid = false;
        const auto category = parseCategory(*categoryText);
        if (!category) {
            addIssue(issues, RuleIssueCode::InvalidValue, source,
                     prefix + QStringLiteral(".category"),
                     QStringLiteral("未知数据分类枚举值"));
            valid = false;
        }
        const auto risk = parseRisk(*riskText);
        if (!risk) {
            addIssue(issues, RuleIssueCode::InvalidValue, source,
                     prefix + QStringLiteral(".risk"),
                     QStringLiteral("未知风险枚举值"));
            valid = false;
        }
        const auto rebuildable = parseRebuildable(
                entryObject.value(QStringLiteral("rebuildable")));
        if (!rebuildable) {
            addIssue(issues, RuleIssueCode::InvalidValue, source,
                     prefix + QStringLiteral(".rebuildable"),
                     QStringLiteral("值必须是布尔值或 unknown"));
            valid = false;
        }
        for (const QString &path : std::as_const(normalizedPaths)) {
            const QString pathKey = path.toCaseFolded();
            if (seenPaths.contains(pathKey)) {
                addIssue(issues, RuleIssueCode::InvalidValue, source,
                         prefix + QStringLiteral(".paths"),
                         QStringLiteral("分类路径与同一规则中的其他条目重复"));
                valid = false;
            }
        }

        const QString firstPath = normalizedPaths.isEmpty()
                ? QString() : normalizedPaths.constFirst();
        if (!firstPath.isEmpty() && risk && firstPath == QStringLiteral(".")
                && *risk == RiskLevel::Safe) {
            addIssue(issues, RuleIssueCode::InvalidValue, source,
                     prefix + QStringLiteral(".risk"),
                     QStringLiteral("位置根目录不能声明为可安全清理"));
            valid = false;
        }
        if (risk && rebuildable && *risk == RiskLevel::Safe
                && *rebuildable != RebuildableState::Yes) {
            addIssue(issues, RuleIssueCode::InvalidValue, source,
                     prefix + QStringLiteral(".rebuildable"),
                     QStringLiteral("safe 条目必须明确可重新生成"));
            valid = false;
        }
        if (risk && category && *risk == RiskLevel::Safe
                && *category != DataCategory::Cache
                && *category != DataCategory::Temp
                && *category != DataCategory::DownloadedResource) {
            addIssue(issues, RuleIssueCode::InvalidValue, source,
                     prefix + QStringLiteral(".category"),
                     QStringLiteral("safe 条目只能用于缓存、临时内容或可重新下载资源"));
            valid = false;
        }
        if (risk && category
                && (*category == DataCategory::Credential
                    || *category == DataCategory::Cookie
                    || *category == DataCategory::Database
                    || *category == DataCategory::Session
                    || *category == DataCategory::UserData
                    || *category == DataCategory::Workspace
                    || *category == DataCategory::SaveGame)
                && (*risk == RiskLevel::Safe || *risk == RiskLevel::Low)) {
            addIssue(issues, RuleIssueCode::InvalidValue, source,
                     prefix + QStringLiteral(".risk"),
                     QStringLiteral("敏感或用户数据条目不能声明为低风险"));
            valid = false;
        }

        if (!valid)
            continue;
        seenIds.insert(*id);
        for (const QString &path : std::as_const(normalizedPaths))
            seenPaths.insert(path.toCaseFolded());
        RuleEntry entry {*id, firstPath, *category, *risk, *rebuildable, *impact};
        entry.paths = normalizedPaths;
        entries.append(std::move(entry));
    }
}

} // namespace

RuleLoadResult RuleLoader::load(const QByteArray &json, const QString &sourceName)
{
    RuleLoadResult result;
    const QString source = sourceName.isEmpty() ? QStringLiteral("<memory>") : sourceName;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        addIssue(result.issues, RuleIssueCode::JsonParse, source, QStringLiteral("$"),
                 QStringLiteral("JSON 解析失败（偏移 %1）：%2")
                         .arg(parseError.offset)
                         .arg(parseError.errorString()));
        return result;
    }
    if (!document.isObject()) {
        addIssue(result.issues, RuleIssueCode::InvalidRoot, source, QStringLiteral("$"),
                 QStringLiteral("规则文档根节点必须是对象"));
        return result;
    }

    const QJsonObject root = document.object();
    rejectUnknownFields(root,
                        {QStringLiteral("$schema"), QStringLiteral("id"),
                         QStringLiteral("version"), QStringLiteral("name"),
                         QStringLiteral("publisher"),
                         QStringLiteral("applicationCategory"),
                         QStringLiteral("executablePath"), QStringLiteral("installPath"),
                         QStringLiteral("executables"), QStringLiteral("installPaths"),
                         QStringLiteral("identifiers"),
                         QStringLiteral("locations"), QStringLiteral("entries")},
                        {}, source, result.issues);
    if (root.contains(QStringLiteral("$schema"))
            && !root.value(QStringLiteral("$schema")).isString()) {
        addIssue(result.issues, RuleIssueCode::InvalidType, source,
                 QStringLiteral("$schema"), QStringLiteral("字段必须是字符串"));
    }
    ApplicationRule rule;
    const auto id = requiredString(root, QStringLiteral("id"), QStringLiteral("id"),
                                   source, result.issues);
    const auto version = requiredString(root, QStringLiteral("version"),
                                        QStringLiteral("version"), source, result.issues);
    const auto name = requiredString(root, QStringLiteral("name"), QStringLiteral("name"),
                                     source, result.issues);
    const auto publisher = requiredString(root, QStringLiteral("publisher"),
                                          QStringLiteral("publisher"), source, result.issues);
    const auto category = requiredString(root, QStringLiteral("applicationCategory"),
                                         QStringLiteral("applicationCategory"),
                                         source, result.issues);
    QStringList executablePaths;
    QStringList installPaths;
    parsePathAlternatives(
            root, QStringLiteral("executablePath"), QStringLiteral("executables"),
            QStringLiteral("executablePath"), source, result.issues, executablePaths);
    parsePathAlternatives(
            root, QStringLiteral("installPath"), QStringLiteral("installPaths"),
            QStringLiteral("installPath"), source, result.issues, installPaths);

    if (id && !hasSafeIdentifier(*id)) {
        addIssue(result.issues, RuleIssueCode::InvalidValue, source, QStringLiteral("id"),
                 QStringLiteral("应用标识只能包含小写字母、数字、点、下划线和连字符"));
    }
    const auto validateAbsoluteRulePath = [&result, &source](
            const QStringList &paths, const QString &field) {
        for (qsizetype index = 0; index < paths.size(); ++index) {
            const QString &path = paths.at(index);
            const QString pathField = paths.size() == 1
                    ? field : QStringLiteral("%1[%2]").arg(field).arg(index);
            if (path != path.trimmed()) {
                addIssue(result.issues, RuleIssueCode::UnsafePath, source, pathField,
                         QStringLiteral("规则路径不安全：路径首尾不能包含空白字符"));
                continue;
            }
            QString error;
            if (!validateRulePath(path, &error)) {
                addIssue(result.issues, RuleIssueCode::UnsafePath, source, pathField,
                         QStringLiteral("规则路径不安全：%1").arg(error));
            }
        }
    };
    validateAbsoluteRulePath(executablePaths, QStringLiteral("executablePath"));
    validateAbsoluteRulePath(installPaths, QStringLiteral("installPath"));

    parseIdentifiers(root, source, rule.identifiers, result.issues);
    parseLocations(root, source, rule.locations, result.issues);
    parseEntries(root, source, rule.entries, result.issues);
    const bool hasExclusiveLocation = std::any_of(
            rule.locations.cbegin(), rule.locations.cend(),
            [](const RuleLocation &location) {
        return location.ownership == RuleLocationOwnership::Exclusive;
    });
    const bool hasSafeCleanupEntry = std::any_of(
            rule.entries.cbegin(), rule.entries.cend(),
            [](const RuleEntry &entry) {
        return entry.risk == RiskLevel::Safe
                && entry.rebuildable == RebuildableState::Yes;
    });
    if (hasExclusiveLocation && hasSafeCleanupEntry
            && rule.identifiers.runningProcessNames.isEmpty()) {
        addIssue(result.issues, RuleIssueCode::MissingField, source,
                 QStringLiteral("identifiers.runningProcessNames"),
                 QStringLiteral("专属目录包含安全清理条目时必须声明运行进程名"));
    }
    if (!result.issues.isEmpty())
        return result;

    rule.id = *id;
    rule.version = *version;
    rule.name = *name;
    rule.publisher = *publisher;
    rule.category = *category;
    rule.executablePaths = executablePaths;
    rule.installPaths = installPaths;
    rule.executablePath = executablePaths.constFirst();
    rule.installPath = installPaths.constFirst();
    rule.sourceName = source;
    rule.origin = originForSource(source);
    rule.trustLevel = trustLevelForOrigin(rule.origin);
    result.rule = std::move(rule);
    return result;
}

} // namespace wam::core::rules
