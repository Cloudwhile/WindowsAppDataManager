#include "CleanupPlanModel.h"

#include <QLocale>

#include <algorithm>

namespace wam::qmlmodels {
namespace {

QString formatSize(quint64 bytes)
{
    static const QStringList units {
        QStringLiteral("B"), QStringLiteral("KB"), QStringLiteral("MB"),
        QStringLiteral("GB"), QStringLiteral("TB")
    };
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < units.size() - 1) {
        value /= 1024.0;
        ++unit;
    }
    const int decimals = unit == 0 || value >= 100.0 ? 0 : value >= 10.0 ? 1 : 2;
    return QStringLiteral("%1 %2").arg(
            QLocale().toString(value, 'f', decimals), units.at(unit));
}

QString categoryText(DataCategory category)
{
    switch (category) {
    case DataCategory::Cache: return QStringLiteral("缓存");
    case DataCategory::Log: return QStringLiteral("日志");
    case DataCategory::Temp: return QStringLiteral("临时文件");
    case DataCategory::CrashDump: return QStringLiteral("崩溃转储");
    case DataCategory::Config: return QStringLiteral("配置");
    case DataCategory::Database: return QStringLiteral("数据库");
    case DataCategory::Session: return QStringLiteral("会话");
    case DataCategory::Cookie: return QStringLiteral("Cookie");
    case DataCategory::Credential: return QStringLiteral("凭据");
    case DataCategory::UserData: return QStringLiteral("用户数据");
    case DataCategory::Workspace: return QStringLiteral("工作区");
    case DataCategory::SaveGame: return QStringLiteral("游戏存档");
    case DataCategory::DownloadedResource: return QStringLiteral("下载资源");
    case DataCategory::Extension: return QStringLiteral("扩展");
    case DataCategory::Unknown: return QStringLiteral("未知");
    }
    return QStringLiteral("未知");
}

QString stateText(CleanupItemState state)
{
    switch (state) {
    case CleanupItemState::Pending: return QStringLiteral("等待确认");
    case CleanupItemState::Validating: return QStringLiteral("正在验证");
    case CleanupItemState::Ready: return QStringLiteral("验证通过");
    case CleanupItemState::Cleaning: return QStringLiteral("正在清理");
    case CleanupItemState::Done: return QStringLiteral("已完成");
    case CleanupItemState::Skipped: return QStringLiteral("已跳过");
    case CleanupItemState::Failed: return QStringLiteral("未处理");
    }
    return QStringLiteral("未处理");
}

QString riskText(RiskLevel risk)
{
    return risk == RiskLevel::Safe ? QStringLiteral("安全")
                                   : QStringLiteral("不符合计划");
}

} // namespace

CleanupPlanModel::CleanupPlanModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int CleanupPlanModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_plan.items.size();
}

QVariant CleanupPlanModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_plan.items.size())
        return {};
    const CleanupPlanItem &item = m_plan.items.at(index.row());
    switch (role) {
    case CandidateIdRole: return item.candidate.id;
    case ApplicationNameRole: return item.candidate.applicationName;
    case RuleEntryIdRole: return item.candidate.ruleEntryId;
    case PathRole: return item.candidate.path;
    case CategoryTextRole: return categoryText(item.candidate.category);
    case RiskTextRole: return riskText(item.candidate.risk);
    case SizeTextRole: return formatSize(item.candidate.size);
    case SizeValueRole: return static_cast<double>(item.candidate.size);
    case FileCountRole: return static_cast<double>(item.candidate.fileCount);
    case ImpactRole: return item.candidate.impact;
    case SelectedRole: return item.selected;
    case StateRole: return static_cast<int>(item.state);
    case StateTextRole: return stateText(item.state);
    case StatusMessageRole: return item.statusMessage;
    case ReleasedSizeTextRole: return formatSize(item.releasedSize);
    default: return {};
    }
}

QHash<int, QByteArray> CleanupPlanModel::roleNames() const
{
    return {
        {CandidateIdRole, "candidateId"},
        {ApplicationNameRole, "applicationName"},
        {RuleEntryIdRole, "ruleEntryId"},
        {PathRole, "path"},
        {CategoryTextRole, "categoryText"},
        {RiskTextRole, "riskText"},
        {SizeTextRole, "sizeText"},
        {SizeValueRole, "sizeValue"},
        {FileCountRole, "fileCount"},
        {ImpactRole, "impact"},
        {SelectedRole, "selected"},
        {StateRole, "state"},
        {StateTextRole, "stateText"},
        {StatusMessageRole, "statusMessage"},
        {ReleasedSizeTextRole, "releasedSizeText"}
    };
}

int CleanupPlanModel::count() const { return m_plan.items.size(); }

int CleanupPlanModel::selectedCount() const
{
    return static_cast<int>(std::count_if(
            m_plan.items.cbegin(), m_plan.items.cend(), [](const auto &item) {
        return item.selected;
    }));
}

QString CleanupPlanModel::selectedSizeText() const
{
    quint64 size = 0;
    for (const CleanupPlanItem &item : m_plan.items) {
        if (item.selected)
            size += item.candidate.size;
    }
    return formatSize(size);
}

QString CleanupPlanModel::estimatedSizeText() const
{
    return formatSize(m_plan.estimatedSize);
}

QString CleanupPlanModel::releasedSizeText() const
{
    quint64 size = 0;
    for (const CleanupPlanItem &item : m_plan.items)
        size += item.releasedSize;
    return formatSize(size);
}

int CleanupPlanModel::excludedCount() const { return m_plan.excludedCount; }
QStringList CleanupPlanModel::exclusionReasons() const { return m_plan.exclusionReasons; }
const CleanupPlan &CleanupPlanModel::plan() const { return m_plan; }

void CleanupPlanModel::setPlan(CleanupPlan plan)
{
    beginResetModel();
    m_plan = std::move(plan);
    endResetModel();
    emit summaryChanged();
}

void CleanupPlanModel::updateItem(int index,
                                  CleanupItemState state,
                                  QString message,
                                  quint64 releasedSize)
{
    if (index < 0 || index >= m_plan.items.size())
        return;
    CleanupPlanItem &item = m_plan.items[index];
    item.state = state;
    item.statusMessage = std::move(message);
    item.releasedSize = releasedSize;
    emit dataChanged(this->index(index), this->index(index),
                     {StateRole, StateTextRole, StatusMessageRole,
                      ReleasedSizeTextRole});
    emit summaryChanged();
}

QVariantMap CleanupPlanModel::get(int index) const
{
    if (index < 0 || index >= m_plan.items.size())
        return {};
    return itemMap(m_plan.items.at(index));
}

void CleanupPlanModel::setSelected(int index, bool selected)
{
    if (index < 0 || index >= m_plan.items.size())
        return;
    CleanupPlanItem &item = m_plan.items[index];
    if (item.selected == selected || item.state != CleanupItemState::Pending)
        return;
    item.selected = selected;
    emit dataChanged(this->index(index), this->index(index), {SelectedRole});
    emit summaryChanged();
}

QVariantMap CleanupPlanModel::itemMap(const CleanupPlanItem &item) const
{
    QVariantMap map;
    map.insert(QStringLiteral("candidateId"), item.candidate.id);
    map.insert(QStringLiteral("applicationName"), item.candidate.applicationName);
    map.insert(QStringLiteral("ruleEntryId"), item.candidate.ruleEntryId);
    map.insert(QStringLiteral("path"), item.candidate.path);
    map.insert(QStringLiteral("categoryText"), categoryText(item.candidate.category));
    map.insert(QStringLiteral("riskText"), riskText(item.candidate.risk));
    map.insert(QStringLiteral("sizeText"), formatSize(item.candidate.size));
    map.insert(QStringLiteral("sizeValue"), static_cast<double>(item.candidate.size));
    map.insert(QStringLiteral("fileCount"), static_cast<double>(item.candidate.fileCount));
    map.insert(QStringLiteral("impact"), item.candidate.impact);
    map.insert(QStringLiteral("selected"), item.selected);
    map.insert(QStringLiteral("state"), static_cast<int>(item.state));
    map.insert(QStringLiteral("stateText"), stateText(item.state));
    map.insert(QStringLiteral("statusMessage"), item.statusMessage);
    map.insert(QStringLiteral("releasedSizeText"), formatSize(item.releasedSize));
    return map;
}

} // namespace wam::qmlmodels
