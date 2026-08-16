#include "ApplicationListModel.h"

#include <QLocale>

#include <algorithm>

namespace wam::qmlmodels {
namespace {

QString formatSize(quint64 bytes)
{
    static constexpr quint64 kibibyte = 1024;
    static constexpr quint64 mebibyte = kibibyte * 1024;
    static constexpr quint64 gibibyte = mebibyte * 1024;
    static constexpr quint64 tebibyte = gibibyte * 1024;
    const QLocale locale;
    if (bytes >= tebibyte)
        return locale.toString(static_cast<double>(bytes) / tebibyte, 'f', 2) + QStringLiteral(" TB");
    if (bytes >= gibibyte)
        return locale.toString(static_cast<double>(bytes) / gibibyte, 'f', 2) + QStringLiteral(" GB");
    if (bytes >= mebibyte)
        return locale.toString(static_cast<double>(bytes) / mebibyte, 'f', bytes < 10 * mebibyte ? 1 : 0)
                + QStringLiteral(" MB");
    if (bytes >= kibibyte)
        return locale.toString(static_cast<double>(bytes) / kibibyte, 'f', 0) + QStringLiteral(" KB");
    return locale.toString(bytes) + QStringLiteral(" B");
}

QString formatCount(quint64 value)
{
    return QLocale().toString(value);
}

QString shortName(const QString &name)
{
    const QStringList words = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.size() >= 2)
        return words[0].left(1).toUpper() + words[1].left(1).toUpper();
    return name.left(name.size() > 1 ? 2 : 1).toUpper();
}

QString modifiedText(const QDateTime &modified)
{
    if (!modified.isValid())
        return QStringLiteral("未知");
    const QDate today = QDate::currentDate();
    const qint64 days = modified.date().daysTo(today);
    if (days <= 0)
        return QStringLiteral("今天 %1").arg(modified.time().toString(QStringLiteral("HH:mm")));
    if (days == 1)
        return QStringLiteral("昨天 %1").arg(modified.time().toString(QStringLiteral("HH:mm")));
    if (days < 30)
        return QStringLiteral("%1 天前").arg(days);
    return QLocale().toString(modified.date(), QLocale::ShortFormat);
}

QString riskText(RiskLevel risk)
{
    switch (risk) {
    case RiskLevel::Safe: return QStringLiteral("安全");
    case RiskLevel::Low: return QStringLiteral("低风险");
    case RiskLevel::Caution: return QStringLiteral("需确认");
    case RiskLevel::High: return QStringLiteral("高风险");
    case RiskLevel::Protected: return QStringLiteral("受保护");
    case RiskLevel::Unknown: return QStringLiteral("未知");
    }
    return QStringLiteral("未知");
}

QString installStateText(InstallState state)
{
    switch (state) {
    case InstallState::Installed: return QStringLiteral("已安装");
    case InstallState::PotentialOrphan: return QStringLiteral("潜在残留");
    case InstallState::Unknown: return QStringLiteral("状态未知");
    }
    return QStringLiteral("状态未知");
}

QString categoryText(DataCategory category)
{
    switch (category) {
    case DataCategory::Cache: return QStringLiteral("缓存");
    case DataCategory::Log: return QStringLiteral("日志");
    case DataCategory::Temp: return QStringLiteral("临时数据");
    case DataCategory::CrashDump: return QStringLiteral("崩溃报告");
    case DataCategory::Config: return QStringLiteral("配置");
    case DataCategory::Database: return QStringLiteral("数据库");
    case DataCategory::Session: return QStringLiteral("会话与本地状态");
    case DataCategory::Cookie: return QStringLiteral("Cookie");
    case DataCategory::Credential: return QStringLiteral("凭据与密钥");
    case DataCategory::UserData: return QStringLiteral("用户数据");
    case DataCategory::Workspace: return QStringLiteral("工作区");
    case DataCategory::SaveGame: return QStringLiteral("游戏存档");
    case DataCategory::DownloadedResource: return QStringLiteral("已下载资源");
    case DataCategory::Extension: return QStringLiteral("扩展与插件");
    case DataCategory::Unknown: return QStringLiteral("尚未分类");
    }
    return QStringLiteral("尚未分类");
}

QString rebuildableText(RebuildableState state)
{
    switch (state) {
    case RebuildableState::Yes: return QStringLiteral("可重新生成");
    case RebuildableState::No: return QStringLiteral("不可重建");
    case RebuildableState::Unknown: return QStringLiteral("尚不确定");
    }
    return QStringLiteral("尚不确定");
}

QString evidenceSourceText(EvidenceSource source)
{
    switch (source) {
    case EvidenceSource::Registry: return QStringLiteral("注册表");
    case EvidenceSource::Appx: return QStringLiteral("AppX / MSIX");
    case EvidenceSource::Executable: return QStringLiteral("可执行文件");
    case EvidenceSource::Publisher: return QStringLiteral("发布者");
    case EvidenceSource::Folder: return QStringLiteral("目录名称");
    case EvidenceSource::Rule: return QStringLiteral("目录规则");
    case EvidenceSource::RunningProcess: return QStringLiteral("运行进程");
    }
    return QStringLiteral("未知来源");
}

QString evidenceStatusText(EvidenceStatus status)
{
    switch (status) {
    case EvidenceStatus::Matched: return QStringLiteral("匹配");
    case EvidenceStatus::Partial: return QStringLiteral("弱匹配");
    case EvidenceStatus::Unavailable: return QStringLiteral("不可用");
    case EvidenceStatus::Conflict: return QStringLiteral("冲突");
    case EvidenceStatus::NotFound: return QStringLiteral("未找到");
    case EvidenceStatus::Incomplete: return QStringLiteral("部分可用");
    case EvidenceStatus::Ambiguous: return QStringLiteral("多项匹配");
    }
    return QStringLiteral("不可用");
}

int accentIndexFor(const QString &id)
{
    constexpr size_t paletteSize = 6;
    return static_cast<int>(qHash(id) % paletteSize);
}

QVariantList groupMaps(const ApplicationInfo &application)
{
    QVariantList result;
    result.reserve(application.dataGroups.size());
    for (const DataGroupInfo &group : application.dataGroups) {
        QVariantMap map;
        map.insert(QStringLiteral("groupId"), group.id);
        map.insert(QStringLiteral("categoryText"), categoryText(group.category));
        map.insert(QStringLiteral("sizeText"), formatSize(group.size));
        map.insert(QStringLiteral("ratio"), application.totalSize > 0
                   ? static_cast<double>(group.size) / application.totalSize : 0.0);
        map.insert(QStringLiteral("fileCountText"), formatCount(group.fileCount));
        map.insert(QStringLiteral("riskLevel"), static_cast<int>(group.risk));
        map.insert(QStringLiteral("riskText"), riskText(group.risk));
        map.insert(QStringLiteral("rebuildableState"), static_cast<int>(group.rebuildable));
        map.insert(QStringLiteral("rebuildableText"), rebuildableText(group.rebuildable));
        map.insert(QStringLiteral("impactText"), group.impact);
        map.insert(QStringLiteral("path"), group.path);
        map.insert(QStringLiteral("ruleSource"), group.ruleSource);
        result.append(map);
    }
    return result;
}

QVariantList evidenceMaps(const ApplicationInfo &application)
{
    QVariantList result;
    result.reserve(application.evidence.size());
    for (const EvidenceInfo &evidence : application.evidence) {
        QVariantMap map;
        map.insert(QStringLiteral("sourceText"), evidenceSourceText(evidence.source));
        map.insert(QStringLiteral("status"), static_cast<int>(evidence.status));
        map.insert(QStringLiteral("statusText"), evidenceStatusText(evidence.status));
        map.insert(QStringLiteral("detail"), evidence.detail);
        result.append(map);
    }
    return result;
}

} // namespace

ApplicationListModel::ApplicationListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ApplicationListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_applications.size();
}

QVariant ApplicationListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_applications.size())
        return {};

    const ApplicationInfo &application = m_applications.at(index.row());
    switch (role) {
    case AppIdRole: return application.id;
    case AppNameRole: return application.name;
    case ShortNameRole: return shortName(application.name);
    case PublisherRole: return application.publisher;
    case CategoryRole: return application.category;
    case LocationRole: return application.location;
    case ExecutablePathRole: return application.executablePath;
    case InstallPathRole: return application.installPath;
    case InstallStateRole: return static_cast<int>(application.installState);
    case InstallStateTextRole: return installStateText(application.installState);
    case ConfidenceRole: return application.confidence;
    case SizeTextRole: return formatSize(application.totalSize);
    case SizeValueRole: return static_cast<double>(application.totalSize);
    case FileCountRole: return formatCount(application.fileCount);
    case ModifiedRole: return modifiedText(application.lastModified);
    case RiskTextRole: return riskText(application.risk);
    case RiskLevelRole: return static_cast<int>(application.risk);
    case ReclaimableTextRole: return formatSize(application.reclaimableSize);
    case ProtectedSizeTextRole: return formatSize(application.protectedSize);
    case UnknownSizeTextRole: return formatSize(application.unknownSize);
    case AccentIndexRole: return accentIndexFor(application.id);
    case SummaryRole: return application.summary;
    case DataGroupsRole: return groupMaps(application);
    case EvidenceRole: return evidenceMaps(application);
    default: return {};
    }
}

QHash<int, QByteArray> ApplicationListModel::roleNames() const
{
    return {
        {AppIdRole, "appId"}, {AppNameRole, "appName"}, {ShortNameRole, "shortName"},
        {PublisherRole, "publisher"}, {CategoryRole, "category"}, {LocationRole, "location"},
        {ExecutablePathRole, "executablePath"}, {InstallPathRole, "installPath"},
        {InstallStateRole, "installState"}, {InstallStateTextRole, "installStateText"},
        {ConfidenceRole, "confidence"}, {SizeTextRole, "sizeText"}, {SizeValueRole, "sizeValue"},
        {FileCountRole, "fileCount"}, {ModifiedRole, "modified"}, {RiskTextRole, "riskText"},
        {RiskLevelRole, "riskLevel"}, {ReclaimableTextRole, "reclaimableText"},
        {ProtectedSizeTextRole, "protectedSizeText"}, {UnknownSizeTextRole, "unknownSizeText"},
        {AccentIndexRole, "accentIndex"}, {SummaryRole, "summary"},
        {DataGroupsRole, "dataGroups"},
        {EvidenceRole, "evidence"}
    };
}

int ApplicationListModel::count() const { return m_applications.size(); }
int ApplicationListModel::revision() const { return m_revision; }
QString ApplicationListModel::totalSizeText() const { return formatSize(m_totalSize); }
QString ApplicationListModel::reclaimableSizeText() const { return formatSize(m_reclaimableSize); }
QString ApplicationListModel::totalFileCountText() const { return formatCount(m_totalFileCount); }
QString ApplicationListModel::protectedSizeText() const { return formatSize(m_protectedSize); }

QString ApplicationListModel::reviewSizeText() const
{
    const quint64 classified = std::min(m_totalSize, m_reclaimableSize + m_protectedSize);
    return formatSize(m_totalSize - classified);
}

double ApplicationListModel::reclaimableRatio() const
{
    return m_totalSize > 0 ? 100.0 * m_reclaimableSize / m_totalSize : 0.0;
}

double ApplicationListModel::protectedRatio() const
{
    return m_totalSize > 0 ? 100.0 * m_protectedSize / m_totalSize : 0.0;
}

double ApplicationListModel::reviewRatio() const
{
    return std::max(0.0, 100.0 - reclaimableRatio() - protectedRatio());
}
int ApplicationListModel::recognizedCount() const { return m_recognizedCount; }
int ApplicationListModel::potentialOrphanCount() const { return m_potentialOrphanCount; }

double ApplicationListModel::maximumSizeValue() const
{
    return m_applications.isEmpty() ? 1.0 : static_cast<double>(m_applications.constFirst().totalSize);
}

QVariantMap ApplicationListModel::get(int index) const
{
    if (index < 0 || index >= m_applications.size())
        return {};
    return applicationMap(m_applications.at(index));
}

int ApplicationListModel::indexOfId(const QString &applicationId) const
{
    if (applicationId.isEmpty())
        return -1;

    const auto iterator = std::find_if(
            m_applications.cbegin(), m_applications.cend(),
            [&applicationId](const ApplicationInfo &application) {
        return application.id == applicationId;
    });
    return iterator == m_applications.cend()
            ? -1 : static_cast<int>(std::distance(m_applications.cbegin(), iterator));
}

void ApplicationListModel::setApplications(QVector<ApplicationInfo> applications)
{
    std::sort(applications.begin(), applications.end(), [](const auto &left, const auto &right) {
        return left.totalSize > right.totalSize;
    });
    const bool countWillChange = applications.size() != m_applications.size();
    beginResetModel();
    m_applications = std::move(applications);
    updateSummary();
    endResetModel();
    ++m_revision;
    if (countWillChange)
        emit countChanged();
    emit revisionChanged();
    emit summaryChanged();
}

void ApplicationListModel::clear()
{
    setApplications({});
}

QVariantMap ApplicationListModel::applicationMap(const ApplicationInfo &application) const
{
    QVariantMap map;
    map.insert(QStringLiteral("appId"), application.id);
    map.insert(QStringLiteral("appName"), application.name);
    map.insert(QStringLiteral("shortName"), shortName(application.name));
    map.insert(QStringLiteral("publisher"), application.publisher);
    map.insert(QStringLiteral("category"), application.category);
    map.insert(QStringLiteral("location"), application.location);
    map.insert(QStringLiteral("executablePath"), application.executablePath);
    map.insert(QStringLiteral("installPath"), application.installPath);
    map.insert(QStringLiteral("installState"), static_cast<int>(application.installState));
    map.insert(QStringLiteral("installStateText"), installStateText(application.installState));
    map.insert(QStringLiteral("confidence"), application.confidence);
    map.insert(QStringLiteral("sizeText"), formatSize(application.totalSize));
    map.insert(QStringLiteral("sizeValue"), static_cast<double>(application.totalSize));
    map.insert(QStringLiteral("fileCount"), formatCount(application.fileCount));
    map.insert(QStringLiteral("modified"), modifiedText(application.lastModified));
    map.insert(QStringLiteral("riskText"), riskText(application.risk));
    map.insert(QStringLiteral("riskLevel"), static_cast<int>(application.risk));
    map.insert(QStringLiteral("reclaimableText"), formatSize(application.reclaimableSize));
    map.insert(QStringLiteral("protectedSizeText"), formatSize(application.protectedSize));
    map.insert(QStringLiteral("unknownSizeText"), formatSize(application.unknownSize));
    map.insert(QStringLiteral("accentIndex"), accentIndexFor(application.id));
    map.insert(QStringLiteral("summary"), application.summary);
    map.insert(QStringLiteral("dataGroups"), groupMaps(application));
    map.insert(QStringLiteral("evidence"), evidenceMaps(application));
    return map;
}

void ApplicationListModel::updateSummary()
{
    m_totalSize = 0;
    m_reclaimableSize = 0;
    m_totalFileCount = 0;
    m_protectedSize = 0;
    m_recognizedCount = 0;
    m_potentialOrphanCount = 0;
    for (const ApplicationInfo &application : std::as_const(m_applications)) {
        m_totalSize += application.totalSize;
        m_reclaimableSize += application.reclaimableSize;
        m_totalFileCount += application.fileCount;
        m_protectedSize += application.protectedSize;
        if (application.confidence >= 50)
            ++m_recognizedCount;
        if (application.installState == InstallState::PotentialOrphan)
            ++m_potentialOrphanCount;
    }
}

} // namespace wam::qmlmodels
