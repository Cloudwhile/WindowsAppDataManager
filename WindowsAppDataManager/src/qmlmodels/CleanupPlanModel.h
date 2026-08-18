#pragma once

#include "../models/CleanupPlan.h"

#include <QAbstractListModel>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace wam::qmlmodels {

class CleanupPlanModel : public QAbstractListModel {
    Q_OBJECT
    QML_NAMED_ELEMENT(CleanupPlanModel)
    QML_UNCREATABLE("由 CleanupViewModel 提供")
    Q_PROPERTY(int count READ count NOTIFY summaryChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY summaryChanged)
    Q_PROPERTY(QString selectedSizeText READ selectedSizeText NOTIFY summaryChanged)
    Q_PROPERTY(QString estimatedSizeText READ estimatedSizeText NOTIFY summaryChanged)
    Q_PROPERTY(QString releasedSizeText READ releasedSizeText NOTIFY summaryChanged)
    Q_PROPERTY(int excludedCount READ excludedCount NOTIFY summaryChanged)
    Q_PROPERTY(QStringList exclusionReasons READ exclusionReasons NOTIFY summaryChanged)

public:
    enum Role {
        CandidateIdRole = Qt::UserRole + 1,
        ApplicationNameRole,
        RuleEntryIdRole,
        PathRole,
        CategoryTextRole,
        RiskTextRole,
        SizeTextRole,
        SizeValueRole,
        FileCountRole,
        ImpactRole,
        SelectedRole,
        StateRole,
        StateTextRole,
        StatusMessageRole,
        ReleasedSizeTextRole
    };
    Q_ENUM(Role)

    explicit CleanupPlanModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const;
    [[nodiscard]] int selectedCount() const;
    [[nodiscard]] QString selectedSizeText() const;
    [[nodiscard]] QString estimatedSizeText() const;
    [[nodiscard]] QString releasedSizeText() const;
    [[nodiscard]] int excludedCount() const;
    [[nodiscard]] QStringList exclusionReasons() const;

    [[nodiscard]] const CleanupPlan &plan() const;
    void setPlan(CleanupPlan plan);
    void updateItem(int index,
                    CleanupItemState state,
                    QString message,
                    quint64 releasedSize);

    Q_INVOKABLE [[nodiscard]] QVariantMap get(int index) const;
    Q_INVOKABLE void setSelected(int index, bool selected);

signals:
    void summaryChanged();

private:
    [[nodiscard]] QVariantMap itemMap(const CleanupPlanItem &item) const;

    CleanupPlan m_plan;
};

} // namespace wam::qmlmodels
