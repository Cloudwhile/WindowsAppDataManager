#include "RuleLoader.h"
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

bool hasSafeIdentifier(const QString &identifier)
{
    static const QRegularExpression pattern(
            QStringLiteral("^[a-z0-9][a-z0-9._-]*$"));
    return pattern.match(identifier).hasMatch();
}

std::optional<QString> normalizedRelativePath(const QString &rawPath,
                                              const QString &field,
                                              const QString &source,
                                              QVector<RuleLoadIssue> &issues)
{
    QString path = rawPath.trimmed();
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));

    static const QRegularExpression drivePrefix(QStringLiteral("^[A-Za-z]:"));
    if (path.isEmpty() || QDir::isAbsolutePath(path) || path.startsWith(QLatin1Char('/'))
            || drivePrefix.match(path).hasMatch()) {
        addIssue(issues, RuleIssueCode::UnsafePath, source, field,
                 QStringLiteral("规则路径必须是非空相对路径"));
        return std::nullopt;
    }

    const QStringList segments = path.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    for (const QString &segment : segments) {
        if (segment.isEmpty() || segment == QStringLiteral(".")
                || segment == QStringLiteral("..") || segment.contains(QLatin1Char(':'))) {
            addIssue(issues, RuleIssueCode::UnsafePath, source, field,
                     QStringLiteral("规则路径包含空段、父级跳转或非法分隔符"));
            return std::nullopt;
        }
    }
    return segments.join(QLatin1Char('/'));
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
                         QStringLiteral("authenticodePublishers")},
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

    QSet<QString> seenLocations;
    for (qsizetype index = 0; index < array.size(); ++index) {
        const QString prefix = QStringLiteral("locations[%1]").arg(index);
        if (!array.at(index).isObject()) {
            addIssue(issues, RuleIssueCode::InvalidType, source, prefix,
                     QStringLiteral("目录项必须是对象"));
            continue;
        }

        const QJsonObject locationObject = array.at(index).toObject();
        rejectUnknownFields(locationObject,
                            {QStringLiteral("scope"), QStringLiteral("path")},
                            prefix, source, issues);
        const auto scopeText = requiredString(locationObject, QStringLiteral("scope"),
                                               prefix + QStringLiteral(".scope"),
                                               source, issues);
        const auto pathText = requiredString(locationObject, QStringLiteral("path"),
                                              prefix + QStringLiteral(".path"),
                                              source, issues);
        if (!scopeText || !pathText)
            continue;

        const auto scope = parseScope(*scopeText);
        if (!scope) {
            addIssue(issues, RuleIssueCode::InvalidValue, source,
                     prefix + QStringLiteral(".scope"),
                     QStringLiteral("未知目录范围，只允许 local、roaming 或 locallow"));
            continue;
        }
        const auto path = normalizedRelativePath(*pathText,
                                                 prefix + QStringLiteral(".path"),
                                                 source, issues);
        if (!path)
            continue;

        const QString locationKey = QString::number(static_cast<int>(*scope))
                + QLatin1Char(':') + path->toCaseFolded();
        if (seenLocations.contains(locationKey)) {
            addIssue(issues, RuleIssueCode::InvalidValue, source, prefix,
                     QStringLiteral("应用目录重复"));
            continue;
        }
        seenLocations.insert(locationKey);
        locations.append({*scope, *path});
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
                             QStringLiteral("category"), QStringLiteral("risk"),
                             QStringLiteral("rebuildable"), QStringLiteral("impact")},
                            prefix, source, issues);
        const auto id = requiredString(entryObject, QStringLiteral("id"),
                                       prefix + QStringLiteral(".id"), source, issues);
        const auto pathText = requiredString(entryObject, QStringLiteral("path"),
                                              prefix + QStringLiteral(".path"), source, issues);
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
        if (!id || !pathText || !categoryText || !riskText || !impact
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

        const auto path = normalizedRelativePath(*pathText,
                                                 prefix + QStringLiteral(".path"),
                                                 source, issues);
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
        if (!path)
            valid = false;

        if (!valid)
            continue;
        seenIds.insert(*id);
        entries.append({*id, *path, *category, *risk, *rebuildable, *impact});
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
    const auto executablePath = requiredString(root, QStringLiteral("executablePath"),
                                               QStringLiteral("executablePath"),
                                               source, result.issues);
    const auto installPath = requiredString(root, QStringLiteral("installPath"),
                                            QStringLiteral("installPath"),
                                            source, result.issues);

    if (id && !hasSafeIdentifier(*id)) {
        addIssue(result.issues, RuleIssueCode::InvalidValue, source, QStringLiteral("id"),
                 QStringLiteral("应用标识只能包含小写字母、数字、点、下划线和连字符"));
    }
    const auto validateAbsoluteRulePath = [&result, &root, &source](
            const std::optional<QString> &path, const QString &field) {
        if (!path)
            return;
        const QString rawPath = root.value(field).toString();
        if (rawPath != rawPath.trimmed()) {
            addIssue(result.issues, RuleIssueCode::UnsafePath, source, field,
                     QStringLiteral("规则路径不安全：路径首尾不能包含空白字符"));
            return;
        }
        QString error;
        if (!validateRulePath(*path, &error)) {
            addIssue(result.issues, RuleIssueCode::UnsafePath, source, field,
                     QStringLiteral("规则路径不安全：%1").arg(error));
        }
    };
    validateAbsoluteRulePath(executablePath, QStringLiteral("executablePath"));
    validateAbsoluteRulePath(installPath, QStringLiteral("installPath"));

    parseIdentifiers(root, source, rule.identifiers, result.issues);
    parseLocations(root, source, rule.locations, result.issues);
    parseEntries(root, source, rule.entries, result.issues);
    if (!result.issues.isEmpty())
        return result;

    rule.id = *id;
    rule.version = *version;
    rule.name = *name;
    rule.publisher = *publisher;
    rule.category = *category;
    rule.executablePath = *executablePath;
    rule.installPath = *installPath;
    rule.sourceName = source;
    result.rule = std::move(rule);
    return result;
}

} // namespace wam::core::rules
