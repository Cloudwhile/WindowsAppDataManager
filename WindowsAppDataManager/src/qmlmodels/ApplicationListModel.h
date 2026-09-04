#pragma once

#include "../models/ApplicationInfo.h"

#include <QAbstractListModel>
#include <QHash>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace wam::qmlmodels {

class ApplicationListModel : public QAbstractListModel {
    Q_OBJECT
    QML_NAMED_ELEMENT(ApplicationListModel)
    QML_UNCREATABLE("由 Backend 提供")
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(QString totalSizeText READ totalSizeText NOTIFY summaryChanged)
    Q_PROPERTY(QString reclaimableSizeText READ reclaimableSizeText NOTIFY summaryChanged)
    Q_PROPERTY(QString totalFileCountText READ totalFileCountText NOTIFY summaryChanged)
    Q_PROPERTY(QString protectedSizeText READ protectedSizeText NOTIFY summaryChanged)
    Q_PROPERTY(QString reviewSizeText READ reviewSizeText NOTIFY summaryChanged)
    Q_PROPERTY(double reclaimableRatio READ reclaimableRatio NOTIFY summaryChanged)
    Q_PROPERTY(double protectedRatio READ protectedRatio NOTIFY summaryChanged)
    Q_PROPERTY(double reviewRatio READ reviewRatio NOTIFY summaryChanged)
    Q_PROPERTY(int recognizedCount READ recognizedCount NOTIFY summaryChanged)
    Q_PROPERTY(int potentialOrphanCount READ potentialOrphanCount NOTIFY summaryChanged)
    Q_PROPERTY(double maximumSizeValue READ maximumSizeValue NOTIFY summaryChanged)

public:
    enum Role {
        AppIdRole = Qt::UserRole + 1,
        AppNameRole,
        ShortNameRole,
        IconSourceRole,
        PublisherRole,
        CategoryRole,
        LocationRole,
        ExecutablePathRole,
        InstallPathRole,
        InstallStateRole,
        InstallStateTextRole,
        ConfidenceRole,
        AttributionStateRole,
        AttributionConfidenceRole,
        InstallationStateRole,
        InstallationConfidenceRole,
        SizeTextRole,
        SizeValueRole,
        FileCountRole,
        ModifiedRole,
        RiskTextRole,
        RiskLevelRole,
        ReclaimableTextRole,
        ProtectedSizeTextRole,
        UnknownSizeTextRole,
        AccentIndexRole,
        SummaryRole,
        OrphanConfidenceRole,
        OrphanSummaryRole,
        OrphanBlockingReasonsRole,
        DataGroupsRole,
        EvidenceRole,
        OwnerKindRole,
        OwnerKindTextRole
    };
    Q_ENUM(Role)

    explicit ApplicationListModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const;
    [[nodiscard]] int revision() const;
    [[nodiscard]] QString totalSizeText() const;
    [[nodiscard]] QString reclaimableSizeText() const;
    [[nodiscard]] QString totalFileCountText() const;
    [[nodiscard]] QString protectedSizeText() const;
    [[nodiscard]] QString reviewSizeText() const;
    [[nodiscard]] double reclaimableRatio() const;
    [[nodiscard]] double protectedRatio() const;
    [[nodiscard]] double reviewRatio() const;
    [[nodiscard]] int recognizedCount() const;
    [[nodiscard]] int potentialOrphanCount() const;
    [[nodiscard]] double maximumSizeValue() const;

    Q_INVOKABLE [[nodiscard]] QVariantMap get(int index) const;
    Q_INVOKABLE [[nodiscard]] QVariantMap getSummary(int index) const;
    Q_INVOKABLE [[nodiscard]] int indexOfId(const QString &applicationId) const;

    [[nodiscard]] const QVector<ApplicationInfo> &applications() const;
    void setApplications(QVector<ApplicationInfo> applications);
    void mergeScanUpdates(QVector<ApplicationInfo> applications);
    void clear();

signals:
    void countChanged();
    void revisionChanged();
    void summaryChanged();

private:
    [[nodiscard]] QVariantMap applicationSummaryMap(
            const ApplicationInfo &application) const;
    [[nodiscard]] QVariantMap applicationMap(const ApplicationInfo &application) const;
    [[nodiscard]] int insertionRowFor(
            const ApplicationInfo &application,
            int excludedRow = -1) const;
    void reindexRows(int firstRow, int lastRow);
    void rebuildRowIndex();
    void addToSummary(const ApplicationInfo &application);
    void replaceInSummary(const ApplicationInfo &previous,
                          const ApplicationInfo &replacement);
    void rebuildMaximumSize();
    void updateSummary();

    QVector<ApplicationInfo> m_applications;
    QHash<QString, int> m_rowsById;
    quint64 m_totalSize = 0;
    quint64 m_reclaimableSize = 0;
    quint64 m_totalFileCount = 0;
    quint64 m_protectedSize = 0;
    quint64 m_maximumSize = 0;
    int m_recognizedCount = 0;
    int m_potentialOrphanCount = 0;
    int m_revision = 0;
};

} // namespace wam::qmlmodels
