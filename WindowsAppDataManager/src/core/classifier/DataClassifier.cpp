#include "DataClassifier.h"

#include "../rules/GlobMatcher.h"

#include <QStringList>

#include <optional>
#include <utility>

namespace wam::core {
namespace {

QStringList components(const QString &normalizedPath)
{
    QStringList result = normalizedPath.split(
            QLatin1Char('/'), Qt::SkipEmptyParts);
    for (QString &component : result)
        component = component.trimmed();
    result.removeAll(QString {});
    return result;
}

QString pathString(const std::filesystem::path &path)
{
#ifdef _WIN32
    const auto &nativePath = path.native();
    return QString::fromWCharArray(
            nativePath.data(), static_cast<qsizetype>(nativePath.size()));
#else
    return QString::fromStdString(path.generic_string());
#endif
}

QString normalizedPath(QString value)
{
    value = value.trimmed().toCaseFolded();
    value.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (value.startsWith(QStringLiteral("./")))
        value.remove(0, 2);
    while (value.endsWith(QLatin1Char('/')))
        value.chop(1);
    return value;
}

QString normalizedPath(const std::filesystem::path &path)
{
    QString value = pathString(path);
#ifndef _WIN32
    value.replace(QLatin1Char('\\'), QLatin1Char('/'));
#endif
    return normalizedPath(std::move(value));
}

QString fileName(const QString &normalizedPath)
{
    const qsizetype separator = normalizedPath.lastIndexOf(QLatin1Char('/'));
    if (separator < 0)
        return normalizedPath.trimmed();
    if (separator + 1 >= normalizedPath.size())
        return {};
    return normalizedPath.sliced(separator + 1).trimmed();
}

QVector<QStringList> normalizedRulePaths(const QVector<RuleEntry> &rules)
{
    QVector<QStringList> result;
    result.reserve(rules.size());
    for (const RuleEntry &entry : rules) {
        QStringList paths = entry.paths;
        if (paths.isEmpty() && !entry.path.isEmpty())
            paths.append(entry.path);
        for (QString &path : paths)
            path = normalizedPath(std::move(path));
        result.append(std::move(paths));
    }
    return result;
}

bool hasComponent(const QStringList &values, const QStringList &candidates)
{
    for (const QString &value : values) {
        if (candidates.contains(value))
            return true;
    }
    return false;
}

bool componentContains(const QStringList &values, const QStringList &needles)
{
    for (const QString &value : values) {
        for (const QString &needle : needles) {
            if (value.contains(needle))
                return true;
        }
    }
    return false;
}

bool hasSuffix(const QString &fileName, const QStringList &suffixes)
{
    for (const QString &suffix : suffixes) {
        if (fileName.endsWith(suffix))
            return true;
    }
    return false;
}

bool pathPrefixMatches(const QString &path, const QString &prefix)
{
    if (prefix == QStringLiteral("."))
        return !path.isEmpty();
    return !prefix.isEmpty()
            && (path == prefix || path.startsWith(prefix + QLatin1Char('/')));
}

Classification makeClassification(QString id,
                                  DataCategory category,
                                  RiskLevel risk,
                                  RebuildableState rebuildable,
                                  QString impact,
                                  QString ruleSource,
                                  QString matchedPath = {},
                                  bool verifiedRule = false)
{
    return {std::move(id), category, risk, rebuildable,
            std::move(impact), std::move(ruleSource),
            std::move(matchedPath), verifiedRule};
}

std::optional<Classification> sensitiveClassification(
        const QStringList &pathComponents,
        const QString &fileName)
{
    if (componentContains(pathComponents,
                          {QStringLiteral("credential"), QStringLiteral("password"),
                           QStringLiteral("keychain"), QStringLiteral("login data")})) {
        return makeClassification(QStringLiteral("credential"), DataCategory::Credential,
                                  RiskLevel::Protected, RebuildableState::No,
                                  QStringLiteral("可能包含凭据、密码或密钥，默认禁止处理。"),
                                  QStringLiteral("启发式 / credential-protection"));
    }

    if (componentContains(pathComponents, {QStringLiteral("cookie")})) {
        return makeClassification(QStringLiteral("cookie"), DataCategory::Cookie,
                                  RiskLevel::High, RebuildableState::No,
                                  QStringLiteral("可能导致退出登录或丢失网站与应用偏好。"),
                                  QStringLiteral("启发式 / cookie-data"));
    }

    if (componentContains(pathComponents,
                          {QStringLiteral("session"), QStringLiteral("local storage"),
                           QStringLiteral("indexeddb")})) {
        return makeClassification(QStringLiteral("session"), DataCategory::Session,
                                  RiskLevel::High, RebuildableState::No,
                                  QStringLiteral("可能导致退出登录或丢失未同步的应用状态。"),
                                  QStringLiteral("启发式 / session-data"));
    }

    if (hasComponent(pathComponents,
                     {QStringLiteral("database"), QStringLiteral("databases")})
            || hasSuffix(fileName, {QStringLiteral(".db"), QStringLiteral(".sqlite"),
                                    QStringLiteral(".sqlite3")})) {
        return makeClassification(QStringLiteral("database"), DataCategory::Database,
                                  RiskLevel::High, RebuildableState::No,
                                  QStringLiteral("数据库可能包含用户数据或应用状态，不能自动清理。"),
                                  QStringLiteral("启发式 / database"));
    }
    return std::nullopt;
}

std::optional<Classification> applicationRuleClassification(
        const QString &path,
        const QVector<RuleEntry> &applicationRules,
        const QVector<QStringList> &normalizedApplicationRulePaths,
        const QString &source)
{
    const RuleEntry *bestMatch = nullptr;
    QString bestPath;
    qsizetype bestSpecificity = -1;
    for (qsizetype index = 0; index < applicationRules.size(); ++index) {
        const RuleEntry &entry = applicationRules.at(index);
        const QStringList &patterns = normalizedApplicationRulePaths.at(index);
        const QStringList declaredPaths = entry.paths.isEmpty()
                ? QStringList {entry.path} : entry.paths;
        for (qsizetype patternIndex = 0; patternIndex < patterns.size(); ++patternIndex) {
            const QString &pattern = patterns.at(patternIndex);
            const bool matched = pattern.contains(QLatin1Char('*'))
                    ? rules::GlobMatcher::matches(pattern, path)
                    : pathPrefixMatches(path, pattern);
            if (!matched)
                continue;

            // 固定字符越多的模式越具体；通配符（尤其 **）不会增加具体度。
            qsizetype specificity = 0;
            for (const QChar character : pattern) {
                if (character != QLatin1Char('*'))
                    ++specificity;
            }
            if (pattern.contains(QStringLiteral("**")))
                specificity -= 2;
            if (specificity <= bestSpecificity)
                continue;
            bestMatch = &entry;
            bestPath = patternIndex < declaredPaths.size()
                    ? declaredPaths.at(patternIndex) : pattern;
            bestSpecificity = specificity;
        }
    }
    if (!bestMatch)
        return std::nullopt;

    const QString effectiveSource = source.isEmpty()
            ? QStringLiteral("应用规则 / %1").arg(bestMatch->id) : source;
    return makeClassification(bestMatch->id, bestMatch->category, bestMatch->risk,
                              bestMatch->rebuildable, bestMatch->impact,
                              effectiveSource, bestPath, true);
}

Classification heuristicClassification(const QStringList &pathComponents,
                                        const QString &fileName)
{
    if (componentContains(pathComponents,
                          {QStringLiteral("workspace"), QStringLiteral("user data"),
                           QStringLiteral("userdata"), QStringLiteral("savegame"),
                           QStringLiteral("save game")})) {
        return makeClassification(QStringLiteral("user-data"), DataCategory::UserData,
                                  RiskLevel::Caution, RebuildableState::No,
                                  QStringLiteral("可能包含工作区、存档或其他用户生成内容。"),
                                  QStringLiteral("启发式 / user-data"));
    }

    if (hasComponent(pathComponents,
                     {QStringLiteral("config"), QStringLiteral("settings"),
                      QStringLiteral("preferences")})
            || hasSuffix(fileName, {QStringLiteral(".ini")})) {
        return makeClassification(QStringLiteral("config"), DataCategory::Config,
                                  RiskLevel::Caution, RebuildableState::No,
                                  QStringLiteral("删除可能重置应用设置或界面状态。"),
                                  QStringLiteral("启发式 / configuration"));
    }

    if (hasComponent(pathComponents,
                     {QStringLiteral("extension"), QStringLiteral("extensions"),
                      QStringLiteral("plugin"), QStringLiteral("plugins")})) {
        return makeClassification(QStringLiteral("extension"), DataCategory::Extension,
                                  RiskLevel::Caution, RebuildableState::Unknown,
                                  QStringLiteral("部分内容可重新下载，但本地扩展状态可能丢失。"),
                                  QStringLiteral("启发式 / extensions"));
    }

    if (hasComponent(pathComponents,
                     {QStringLiteral("cache"), QStringLiteral("caches"),
                      QStringLiteral("gpucache"), QStringLiteral("code cache"),
                      QStringLiteral("code_cache"), QStringLiteral("shadercache"),
                      QStringLiteral("shader cache")})) {
        return makeClassification(QStringLiteral("cache"), DataCategory::Cache,
                                  RiskLevel::Safe, RebuildableState::Yes,
                                  QStringLiteral("删除后应用会按需重新生成，首次启动可能变慢。"),
                                  QStringLiteral("启发式 / cache"));
    }

    if (hasComponent(pathComponents,
                     {QStringLiteral("crash"), QStringLiteral("crashes"),
                      QStringLiteral("crashpad"), QStringLiteral("crash dumps"),
                      QStringLiteral("crashdumps"), QStringLiteral("dumps")})
            || hasSuffix(fileName, {QStringLiteral(".dmp"), QStringLiteral(".mdmp")})) {
        return makeClassification(QStringLiteral("crash-dump"), DataCategory::CrashDump,
                                  RiskLevel::Low, RebuildableState::No,
                                  QStringLiteral("删除后无法继续分析这些旧崩溃，不影响正常运行。"),
                                  QStringLiteral("启发式 / crash-dump"));
    }

    if (hasComponent(pathComponents,
                     {QStringLiteral("log"), QStringLiteral("logs")})
            || hasSuffix(fileName, {QStringLiteral(".log")})) {
        return makeClassification(QStringLiteral("log"), DataCategory::Log,
                                  RiskLevel::Low, RebuildableState::Yes,
                                  QStringLiteral("仅移除旧诊断记录，应用会继续生成新日志。"),
                                  QStringLiteral("启发式 / logs"));
    }

    if (hasComponent(pathComponents,
                     {QStringLiteral("temp"), QStringLiteral("tmp"),
                      QStringLiteral("temporary files")})
            || hasSuffix(fileName, {QStringLiteral(".tmp"), QStringLiteral(".temp")})) {
        return makeClassification(QStringLiteral("temp"), DataCategory::Temp,
                                  RiskLevel::Safe, RebuildableState::Yes,
                                  QStringLiteral("临时内容通常可重新生成，仍需排除锁定文件。"),
                                  QStringLiteral("启发式 / temporary"));
    }

    return makeClassification(QStringLiteral("unknown"), DataCategory::Unknown,
                              RiskLevel::Unknown, RebuildableState::Unknown,
                              QStringLiteral("缺少足够证据，不会自动进入任何清理计划。"),
                              QStringLiteral("启发式 / 未分类"));
}

} // namespace

DataClassifier::DataClassifier(const QVector<RuleEntry> &applicationRules,
                               QString ruleSource)
    : m_applicationRules(applicationRules),
      m_normalizedRulePaths(normalizedRulePaths(applicationRules)),
      m_ruleSource(std::move(ruleSource))
{
}

Classification DataClassifier::classify(const std::filesystem::path &relativePath) const
{
    return classifyNormalizedPath(normalizedPath(relativePath));
}

Classification DataClassifier::classify(
        const std::filesystem::path &relativePath,
        const QVector<RuleEntry> &applicationRules,
        const QString &ruleSource) const
{
    return DataClassifier(applicationRules, ruleSource).classify(relativePath);
}

Classification DataClassifier::classifyNormalizedPath(
        const QString &relativePath) const
{
    const QStringList pathComponents = components(relativePath);
    const QString leafName = fileName(relativePath);

    if (const auto sensitive = sensitiveClassification(pathComponents, leafName))
        return *sensitive;
    if (const auto ruleMatch = applicationRuleClassification(
                relativePath, m_applicationRules,
                m_normalizedRulePaths, m_ruleSource)) {
        return *ruleMatch;
    }
    return heuristicClassification(pathComponents, leafName);
}

} // namespace wam::core
