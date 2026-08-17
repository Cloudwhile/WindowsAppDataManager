#include "ScanReportExporter.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStringConverter>
#include <QTextStream>

#include <algorithm>

namespace wam::services {
namespace {

QString csvField(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + value + QLatin1Char('"');
}

void writeRow(QTextStream &stream, const QStringList &values)
{
    for (qsizetype index = 0; index < values.size(); ++index) {
        if (index > 0)
            stream << QLatin1Char(',');
        stream << csvField(values.at(index));
    }
    stream << Qt::endl;
}

QString riskText(RiskLevel level)
{
    switch (level) {
    case RiskLevel::Safe: return QStringLiteral("安全");
    case RiskLevel::Low: return QStringLiteral("低风险");
    case RiskLevel::Caution: return QStringLiteral("需确认");
    case RiskLevel::High: return QStringLiteral("高风险");
    case RiskLevel::Protected: return QStringLiteral("受保护");
    case RiskLevel::Unknown: return QStringLiteral("未知");
    }
    return QStringLiteral("未知");
}

QString riskCode(RiskLevel level)
{
    switch (level) {
    case RiskLevel::Safe: return QStringLiteral("safe");
    case RiskLevel::Low: return QStringLiteral("low");
    case RiskLevel::Caution: return QStringLiteral("caution");
    case RiskLevel::High: return QStringLiteral("high");
    case RiskLevel::Protected: return QStringLiteral("protected");
    case RiskLevel::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

QString categoryCode(DataCategory category)
{
    switch (category) {
    case DataCategory::Cache: return QStringLiteral("cache");
    case DataCategory::Log: return QStringLiteral("log");
    case DataCategory::Temp: return QStringLiteral("temp");
    case DataCategory::CrashDump: return QStringLiteral("crash-dump");
    case DataCategory::Config: return QStringLiteral("config");
    case DataCategory::Database: return QStringLiteral("database");
    case DataCategory::Session: return QStringLiteral("session");
    case DataCategory::Cookie: return QStringLiteral("cookie");
    case DataCategory::Credential: return QStringLiteral("credential");
    case DataCategory::UserData: return QStringLiteral("user-data");
    case DataCategory::Workspace: return QStringLiteral("workspace");
    case DataCategory::SaveGame: return QStringLiteral("save-game");
    case DataCategory::DownloadedResource: return QStringLiteral("downloaded-resource");
    case DataCategory::Extension: return QStringLiteral("extension");
    case DataCategory::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

QString rebuildableCode(RebuildableState state)
{
    switch (state) {
    case RebuildableState::Yes: return QStringLiteral("yes");
    case RebuildableState::No: return QStringLiteral("no");
    case RebuildableState::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

QString installStateText(InstallState state)
{
    switch (state) {
    case InstallState::Installed: return QStringLiteral("已安装");
    case InstallState::PotentialOrphan: return QStringLiteral("潜在残留");
    case InstallState::Unknown: return QStringLiteral("未识别");
    }
    return QStringLiteral("未识别");
}

QString installStateCode(InstallState state)
{
    switch (state) {
    case InstallState::Installed: return QStringLiteral("installed");
    case InstallState::PotentialOrphan: return QStringLiteral("potential-orphan");
    case InstallState::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

QString evidenceSourceCode(EvidenceSource source)
{
    switch (source) {
    case EvidenceSource::Registry: return QStringLiteral("registry");
    case EvidenceSource::Appx: return QStringLiteral("appx");
    case EvidenceSource::Executable: return QStringLiteral("executable");
    case EvidenceSource::Publisher: return QStringLiteral("publisher");
    case EvidenceSource::Folder: return QStringLiteral("folder");
    case EvidenceSource::Rule: return QStringLiteral("rule");
    case EvidenceSource::RunningProcess: return QStringLiteral("running-process");
    }
    return QStringLiteral("unknown");
}

QString evidenceStatusCode(EvidenceStatus status)
{
    switch (status) {
    case EvidenceStatus::Matched: return QStringLiteral("matched");
    case EvidenceStatus::Partial: return QStringLiteral("partial");
    case EvidenceStatus::Unavailable: return QStringLiteral("unavailable");
    case EvidenceStatus::Conflict: return QStringLiteral("conflict");
    case EvidenceStatus::NotFound: return QStringLiteral("not-found");
    case EvidenceStatus::Incomplete: return QStringLiteral("incomplete");
    case EvidenceStatus::Ambiguous: return QStringLiteral("ambiguous");
    }
    return QStringLiteral("unknown");
}

QString issueCodeText(ScanErrorCode code)
{
    switch (code) {
    case ScanErrorCode::AccessDenied: return QStringLiteral("访问被拒绝");
    case ScanErrorCode::PathUnavailable: return QStringLiteral("路径不可用");
    case ScanErrorCode::IoError: return QStringLiteral("I/O 错误");
    case ScanErrorCode::Cancelled: return QStringLiteral("扫描已取消");
    }
    return QStringLiteral("未知错误");
}

QJsonObject applicationJson(const ApplicationInfo &application)
{
    QJsonObject object {
        {QStringLiteral("id"), application.id},
        {QStringLiteral("name"), application.name},
        {QStringLiteral("publisher"), application.publisher},
        {QStringLiteral("category"), application.category},
        {QStringLiteral("location"), application.location},
        {QStringLiteral("executablePath"), application.executablePath},
        {QStringLiteral("installPath"), application.installPath},
        {QStringLiteral("totalSizeBytes"), static_cast<qint64>(application.totalSize)},
        {QStringLiteral("fileCount"), static_cast<qint64>(application.fileCount)},
        {QStringLiteral("reclaimableBytes"), static_cast<qint64>(application.reclaimableSize)},
        {QStringLiteral("protectedBytes"), static_cast<qint64>(application.protectedSize)},
        {QStringLiteral("unknownBytes"), static_cast<qint64>(application.unknownSize)},
        {QStringLiteral("riskLevel"), static_cast<int>(application.risk)},
        {QStringLiteral("risk"), riskCode(application.risk)},
        {QStringLiteral("riskText"), riskText(application.risk)},
        {QStringLiteral("installStateValue"), static_cast<int>(application.installState)},
        {QStringLiteral("installState"), installStateCode(application.installState)},
        {QStringLiteral("installStateText"), installStateText(application.installState)},
        {QStringLiteral("confidence"), application.confidence},
        {QStringLiteral("lastModified"), application.lastModified.isValid()
             ? application.lastModified.toString(Qt::ISODate) : QString()},
        {QStringLiteral("summary"), application.summary}
    };

    QJsonArray groups;
    for (const DataGroupInfo &group : application.dataGroups) {
        groups.append(QJsonObject {
            {QStringLiteral("id"), group.id},
            {QStringLiteral("category"), categoryCode(group.category)},
            {QStringLiteral("categoryValue"), static_cast<int>(group.category)},
            {QStringLiteral("sizeBytes"), static_cast<qint64>(group.size)},
            {QStringLiteral("fileCount"), static_cast<qint64>(group.fileCount)},
            {QStringLiteral("riskLevel"), static_cast<int>(group.risk)},
            {QStringLiteral("risk"), riskCode(group.risk)},
            {QStringLiteral("riskText"), riskText(group.risk)},
            {QStringLiteral("rebuildableState"), static_cast<int>(group.rebuildable)},
            {QStringLiteral("rebuildable"), rebuildableCode(group.rebuildable)},
            {QStringLiteral("impact"), group.impact},
            {QStringLiteral("path"), group.path},
            {QStringLiteral("ruleSource"), group.ruleSource}
        });
    }
    object.insert(QStringLiteral("dataGroups"), groups);

    QJsonArray evidence;
    for (const EvidenceInfo &item : application.evidence) {
        evidence.append(QJsonObject {
            {QStringLiteral("source"), evidenceSourceCode(item.source)},
            {QStringLiteral("sourceValue"), static_cast<int>(item.source)},
            {QStringLiteral("status"), evidenceStatusCode(item.status)},
            {QStringLiteral("statusValue"), static_cast<int>(item.status)},
            {QStringLiteral("detail"), item.detail}
        });
    }
    object.insert(QStringLiteral("evidence"), evidence);
    return object;
}

QJsonObject issueJson(const ScanIssue &issue)
{
    return {
        {QStringLiteral("path"), issue.path},
        {QStringLiteral("message"), issue.message},
        {QStringLiteral("technicalDetail"), issue.technicalDetail},
        {QStringLiteral("code"), static_cast<int>(issue.code)},
        {QStringLiteral("codeText"), issueCodeText(issue.code)}
    };
}

QString exportJsonReport(const QString &filePath,
                         const QVector<ApplicationInfo> &applications,
                         const QVector<ScanIssue> &issues)
{
    quint64 totalSize = 0;
    quint64 totalFiles = 0;
    quint64 reclaimable = 0;
    quint64 protectedSize = 0;
    quint64 unknownSize = 0;
    QJsonArray applicationRows;
    for (const ApplicationInfo &application : applications) {
        totalSize += application.totalSize;
        totalFiles += application.fileCount;
        reclaimable += application.reclaimableSize;
        protectedSize += application.protectedSize;
        unknownSize += application.unknownSize;
        applicationRows.append(applicationJson(application));
    }

    QJsonArray issueRows;
    for (const ScanIssue &issue : issues)
        issueRows.append(issueJson(issue));

    const quint64 classified = std::min(totalSize, reclaimable + protectedSize);
    const QJsonObject report {
        {QStringLiteral("schemaVersion"), QStringLiteral("1.0")},
        {QStringLiteral("reportType"), QStringLiteral("windows-appdata-manager.scan")},
        {QStringLiteral("generatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("summary"), QJsonObject {
            {QStringLiteral("applicationCount"), applications.size()},
            {QStringLiteral("totalSizeBytes"), static_cast<qint64>(totalSize)},
            {QStringLiteral("totalFileCount"), static_cast<qint64>(totalFiles)},
            {QStringLiteral("reclaimableBytes"), static_cast<qint64>(reclaimable)},
            {QStringLiteral("protectedBytes"), static_cast<qint64>(protectedSize)},
            {QStringLiteral("unknownBytes"), static_cast<qint64>(unknownSize)},
            {QStringLiteral("reviewBytes"), static_cast<qint64>(totalSize - classified)},
            {QStringLiteral("issueCount"), issues.size()}
        }},
        {QStringLiteral("applications"), applicationRows},
        {QStringLiteral("issues"), issueRows}
    };

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return QStringLiteral("无法创建报告：%1").arg(file.errorString());
    const QByteArray contents = QJsonDocument(report).toJson(QJsonDocument::Indented);
    if (file.write(contents) != contents.size())
        return QStringLiteral("无法写入报告：%1").arg(file.errorString());
    if (!file.commit())
        return QStringLiteral("无法保存报告：%1").arg(file.errorString());
    return {};
}

QString exportCsvReport(const QString &filePath,
                        const QVector<ApplicationInfo> &applications,
                        const QVector<ScanIssue> &issues)
{
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return QStringLiteral("无法创建报告：%1").arg(file.errorString());

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream.setGenerateByteOrderMark(true);

    writeRow(stream, {QStringLiteral("记录类型"), QStringLiteral("应用名称"),
                      QStringLiteral("发布者"), QStringLiteral("分类"),
                      QStringLiteral("AppData 位置"), QStringLiteral("占用字节"),
                      QStringLiteral("文件数"), QStringLiteral("风险"),
                      QStringLiteral("安装状态"), QStringLiteral("摘要")});
    for (const ApplicationInfo &application : applications) {
        writeRow(stream, {QStringLiteral("应用"), application.name, application.publisher,
                          application.category, application.location,
                          QString::number(application.totalSize),
                          QString::number(application.fileCount), riskText(application.risk),
                          installStateText(application.installState), application.summary});
    }

    writeRow(stream, {});
    writeRow(stream, {QStringLiteral("记录类型"), QStringLiteral("位置"),
                      QStringLiteral("原因"), QStringLiteral("错误类别"),
                      QStringLiteral("技术详情")});
    for (const ScanIssue &issue : issues) {
        writeRow(stream, {QStringLiteral("未完整读取"), issue.path, issue.message,
                          issueCodeText(issue.code), issue.technicalDetail});
    }

    if (!file.commit())
        return QStringLiteral("无法保存报告：%1").arg(file.errorString());
    return {};
}

} // namespace

QString exportScanReport(const QUrl &destination,
                         const QVector<ApplicationInfo> &applications,
                         const QVector<ScanIssue> &issues)
{
    if (!destination.isLocalFile())
        return QStringLiteral("请选择本地文件夹中的导出位置。");

    const QString filePath = destination.toLocalFile();
    if (filePath.isEmpty())
        return QStringLiteral("导出路径无效。");

    if (QFileInfo(filePath).suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0)
        return exportJsonReport(filePath, applications, issues);
    return exportCsvReport(filePath, applications, issues);
}

} // namespace wam::services
