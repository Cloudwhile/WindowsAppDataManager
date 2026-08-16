#pragma once

#include <QSortFilterProxyModel>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace wam::qmlmodels {

class ApplicationListModel;

class ApplicationFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
    QML_NAMED_ELEMENT(ApplicationFilterModel)
    QML_UNCREATABLE("由 Backend 提供")
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(int riskFilter READ riskFilter WRITE setRiskFilter NOTIFY riskFilterChanged)
    Q_PROPERTY(int installStateFilter READ installStateFilter WRITE setInstallStateFilter
               NOTIFY installStateFilterChanged)
    Q_PROPERTY(int sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending
               NOTIFY sortDescendingChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit ApplicationFilterModel(ApplicationListModel *sourceModel, QObject *parent = nullptr);

    [[nodiscard]] QString searchText() const;
    void setSearchText(const QString &searchText);

    [[nodiscard]] int riskFilter() const;
    void setRiskFilter(int riskFilter);

    [[nodiscard]] int installStateFilter() const;
    void setInstallStateFilter(int installStateFilter);

    [[nodiscard]] int sortMode() const;
    void setSortMode(int sortMode);

    [[nodiscard]] bool sortDescending() const;
    void setSortDescending(bool sortDescending);

    [[nodiscard]] int count() const;
    Q_INVOKABLE [[nodiscard]] QVariantMap get(int proxyIndex) const;
    Q_INVOKABLE [[nodiscard]] bool containsSourceIndex(int sourceIndex) const;

signals:
    void searchTextChanged();
    void riskFilterChanged();
    void installStateFilterChanged();
    void sortModeChanged();
    void sortDescendingChanged();
    void countChanged();

protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow,
                                        const QModelIndex &sourceParent) const override;
    [[nodiscard]] bool lessThan(const QModelIndex &sourceLeft,
                                const QModelIndex &sourceRight) const override;

private:
    QString m_searchText;
    int m_riskFilter = -1;
    int m_installStateFilter = -1;
    int m_sortMode = 0;
    bool m_sortDescending = true;
};

} // namespace wam::qmlmodels
