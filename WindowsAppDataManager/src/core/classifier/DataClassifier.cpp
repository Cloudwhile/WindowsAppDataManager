#include "DataClassifier.h"

#include <QFileInfo>
#include <QStringList>

namespace wam::core {
namespace {

QString pathPart(const std::filesystem::path &path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.generic_wstring()).toLower();
#else
    return QString::fromStdString(path.generic_string()).toLower();
#endif
}

QStringList components(const std::filesystem::path &path)
{
    QStringList result;
    for (const auto &component : path) {
        const QString value = pathPart(component).trimmed();
        if (!value.isEmpty() && value != QStringLiteral("/"))
            result.append(value);
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

Classification makeClassification(QString id,
                                  DataCategory category,
                                  RiskLevel risk,
                                  RebuildableState rebuildable,
                                  QString impact,
                                  QString ruleSource)
{
    return {std::move(id), category, risk, rebuildable,
            std::move(impact), std::move(ruleSource)};
}

} // namespace

Classification DataClassifier::classify(const std::filesystem::path &relativePath) const
{
    const QStringList pathComponents = components(relativePath);
    const QString fileName = pathPart(relativePath.filename());

    if (componentContains(pathComponents,
                          {QStringLiteral("credential"), QStringLiteral("password"),
                           QStringLiteral("keychain"), QStringLiteral("login data")})) {
        return makeClassification(QStringLiteral("credential"), DataCategory::Credential,
                                  RiskLevel::Protected, RebuildableState::No,
                                  QStringLiteral("可能包含凭据、密码或密钥，默认禁止处理。"),
                                  QStringLiteral("启发式 / credential-protection"));
    }

    if (componentContains(pathComponents,
                          {QStringLiteral("cookie"), QStringLiteral("session"),
                           QStringLiteral("local storage"), QStringLiteral("indexeddb")})) {
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

} // namespace wam::core
