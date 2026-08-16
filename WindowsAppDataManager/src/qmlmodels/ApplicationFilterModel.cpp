#include "ApplicationFilterModel.h"

#include "ApplicationListModel.h"

#include <QAbstractItemModel>
#include <QModelIndex>
#include <QVariant>

namespace wam::qmlmodels {
namespace {

int compareStrings(const QString &left, const QString &right, Qt::CaseSensitivity sensitivity)
{
    return QString::compare(left, right, sensitivity);
}

int compareNumbers(double left, double right)
{
    if (left < right)
        return -1;
    if (left > right)
        return 1;
    return 0;
}

int compareIntegers(int left, int right)
{
    if (left < right)
        return -1;
    if (left > right)
        return 1;
    return 0;
}

int riskPriority(int riskLevel)
{
    switch (static_cast<RiskLevel>(riskLevel)) {
    case RiskLevel::Safe: return 0;
    case RiskLevel::Low: return 1;
    case RiskLevel::Unknown: return 2;
    case RiskLevel::Caution: return 3;
    case RiskLevel::High: return 4;
    case RiskLevel::Protected: return 5;
    }
    return 2;
}

} // namespace

ApplicationFilterModel::ApplicationFilterModel(ApplicationListModel *sourceModel, QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setSourceModel(sourceModel);
    setDynamicSortFilter(true);

    connect(this, &QAbstractItemModel::rowsInserted, this, [this] {
        emit countChanged();
    });
    connect(this, &QAbstractItemModel::rowsRemoved, this, [this] {
        emit countChanged();
    });
    connect(this, &QAbstractItemModel::modelReset, this, [this] {
        emit countChanged();
    });

    // lessThan() 处理主字段方向，始终以升序请求排序才能保持并列规则升序。
    sort(0, Qt::AscendingOrder);
}

QString ApplicationFilterModel::searchText() const
{
    return m_searchText;
}

void ApplicationFilterModel::setSearchText(const QString &searchText)
{
    if (m_searchText == searchText)
        return;
    m_searchText = searchText;
    emit searchTextChanged();
    invalidateRowsFilter();
}

int ApplicationFilterModel::riskFilter() const
{
    return m_riskFilter;
}

void ApplicationFilterModel::setRiskFilter(int riskFilter)
{
    if (riskFilter < -1 || riskFilter > 5 || m_riskFilter == riskFilter)
        return;
    m_riskFilter = riskFilter;
    emit riskFilterChanged();
    invalidateRowsFilter();
}

int ApplicationFilterModel::installStateFilter() const
{
    return m_installStateFilter;
}

void ApplicationFilterModel::setInstallStateFilter(int installStateFilter)
{
    if (installStateFilter < -1 || installStateFilter > 2
        || m_installStateFilter == installStateFilter) {
        return;
    }
    m_installStateFilter = installStateFilter;
    emit installStateFilterChanged();
    invalidateRowsFilter();
}

int ApplicationFilterModel::sortMode() const
{
    return m_sortMode;
}

void ApplicationFilterModel::setSortMode(int sortMode)
{
    if (sortMode < 0 || sortMode > 2 || m_sortMode == sortMode)
        return;
    m_sortMode = sortMode;
    emit sortModeChanged();
    invalidate();
    sort(0, Qt::AscendingOrder);
}

bool ApplicationFilterModel::sortDescending() const
{
    return m_sortDescending;
}

void ApplicationFilterModel::setSortDescending(bool sortDescending)
{
    if (m_sortDescending == sortDescending)
        return;
    m_sortDescending = sortDescending;
    emit sortDescendingChanged();
    invalidate();
    sort(0, Qt::AscendingOrder);
}

int ApplicationFilterModel::count() const
{
    return rowCount();
}

QVariantMap ApplicationFilterModel::get(int proxyIndex) const
{
    if (proxyIndex < 0 || proxyIndex >= rowCount())
        return {};

    const QModelIndex sourceIndex = mapToSource(index(proxyIndex, 0));
    const auto *applications = qobject_cast<const ApplicationListModel *>(sourceModel());
    if (!applications || !sourceIndex.isValid())
        return {};

    QVariantMap result = applications->get(sourceIndex.row());
    result.insert(QStringLiteral("sourceIndex"), sourceIndex.row());
    return result;
}

bool ApplicationFilterModel::filterAcceptsRow(int sourceRow,
                                               const QModelIndex &sourceParent) const
{
    const QAbstractItemModel *model = sourceModel();
    if (!model)
        return false;

    const QModelIndex sourceIndex = model->index(sourceRow, 0, sourceParent);
    if (!sourceIndex.isValid())
        return false;

    if (m_riskFilter >= 0
        && model->data(sourceIndex, ApplicationListModel::RiskLevelRole).toInt()
                != m_riskFilter) {
        return false;
    }

    if (m_installStateFilter >= 0
        && model->data(sourceIndex, ApplicationListModel::InstallStateRole).toInt()
                != m_installStateFilter) {
        return false;
    }

    const QString search = m_searchText.trimmed();
    if (search.isEmpty())
        return true;

    return model->data(sourceIndex, ApplicationListModel::AppNameRole)
                   .toString()
                   .contains(search, Qt::CaseInsensitive)
            || model->data(sourceIndex, ApplicationListModel::PublisherRole)
                       .toString()
                       .contains(search, Qt::CaseInsensitive)
            || model->data(sourceIndex, ApplicationListModel::CategoryRole)
                       .toString()
                       .contains(search, Qt::CaseInsensitive);
}

bool ApplicationFilterModel::lessThan(const QModelIndex &sourceLeft,
                                      const QModelIndex &sourceRight) const
{
    const QAbstractItemModel *model = sourceModel();
    if (!model)
        return sourceLeft.row() < sourceRight.row();

    int primaryComparison = 0;
    switch (m_sortMode) {
    case 0:
        primaryComparison = compareNumbers(
                model->data(sourceLeft, ApplicationListModel::SizeValueRole).toDouble(),
                model->data(sourceRight, ApplicationListModel::SizeValueRole).toDouble());
        break;
    case 1:
        primaryComparison = compareStrings(
                model->data(sourceLeft, ApplicationListModel::AppNameRole).toString(),
                model->data(sourceRight, ApplicationListModel::AppNameRole).toString(),
                Qt::CaseInsensitive);
        break;
    case 2:
        primaryComparison = compareIntegers(
                riskPriority(model->data(sourceLeft,
                                         ApplicationListModel::RiskLevelRole).toInt()),
                riskPriority(model->data(sourceRight,
                                         ApplicationListModel::RiskLevelRole).toInt()));
        break;
    default:
        break;
    }

    if (primaryComparison != 0)
        return m_sortDescending ? primaryComparison > 0 : primaryComparison < 0;

    const int nameComparison = compareStrings(
            model->data(sourceLeft, ApplicationListModel::AppNameRole).toString(),
            model->data(sourceRight, ApplicationListModel::AppNameRole).toString(),
            Qt::CaseInsensitive);
    if (nameComparison != 0)
        return nameComparison < 0;

    const int idComparison = compareStrings(
            model->data(sourceLeft, ApplicationListModel::AppIdRole).toString(),
            model->data(sourceRight, ApplicationListModel::AppIdRole).toString(),
            Qt::CaseSensitive);
    if (idComparison != 0)
        return idComparison < 0;

    return sourceLeft.row() < sourceRight.row();
}

} // namespace wam::qmlmodels
