#include "src/qmlmodels/ApplicationFilterModel.h"
#include "src/qmlmodels/ApplicationListModel.h"
#include "src/qmlmodels/ScanViewModel.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <utility>

namespace {

void writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
}

wam::ApplicationInfo application(QString id,
                                 QString name,
                                 QString publisher,
                                 QString category,
                                 quint64 size,
                                 wam::RiskLevel risk,
                                 wam::InstallState installState)
{
    wam::ApplicationInfo result;
    result.id = std::move(id);
    result.name = std::move(name);
    result.publisher = std::move(publisher);
    result.category = std::move(category);
    result.totalSize = size;
    result.risk = risk;
    result.installState = installState;
    return result;
}

} // namespace

class QmlModelsTest final : public QObject {
    Q_OBJECT

private slots:
    void applicationListFindsStableIdsAndExposesAccentIndices();
    void applicationListMergesScanUpdatesWithoutReset();
    void applicationListResolvesStableSelectionAfterSortedMutation();
    void applicationListExposesIndependentOrphanAssessment();
    void applicationFilterCombinesSearchAndExactFilters();
    void applicationFilterSortsAndMapsSourceRows();
    void scanServicePublishesCompletedTargetUpdates();
    void scanServiceBoundsProgressiveUpdateDelivery();
    void viewModelPublishesBackgroundScan();
    void viewModelBatchesLargeScanUpdates();
    void viewModelKeepsInterleavedProgressMonotonic();
    void viewModelPreservesAcceptedIssuesAcrossInterruptedScan();
    void viewModelAppliesBuiltInRules();
};

void QmlModelsTest::applicationListFindsStableIdsAndExposesAccentIndices()
{
    using wam::InstallState;
    using wam::RiskLevel;

    wam::qmlmodels::ApplicationListModel applications;
    applications.setApplications({
        application(QStringLiteral("alpha"), QStringLiteral("Alpha"),
                    QStringLiteral("Vendor"), QStringLiteral("工具"), 10,
                    RiskLevel::Safe, InstallState::Installed),
        application(QStringLiteral("beta"), QStringLiteral("Beta"),
                    QStringLiteral("Vendor"), QStringLiteral("工具"), 20,
                    RiskLevel::Low, InstallState::Installed),
        application(QStringLiteral("gamma"), QStringLiteral("Gamma"),
                    QStringLiteral("Vendor"), QStringLiteral("工具"), 30,
                    RiskLevel::Unknown, InstallState::Unknown)
    });

    QCOMPARE(applications.indexOfId(QStringLiteral("gamma")), 0);
    QCOMPARE(applications.indexOfId(QStringLiteral("beta")), 1);
    QCOMPARE(applications.indexOfId(QStringLiteral("alpha")), 2);
    QCOMPARE(applications.indexOfId(QStringLiteral("missing")), -1);
    QCOMPARE(applications.indexOfId(QString()), -1);

    QCOMPARE(applications.roleNames().value(
                     wam::qmlmodels::ApplicationListModel::AccentIndexRole),
             QByteArray("accentIndex"));
    QCOMPARE(applications.roleNames().value(
                     wam::qmlmodels::ApplicationListModel::IconSourceRole),
             QByteArray("iconSource"));
    for (int row = 0; row < applications.count(); ++row) {
        const QModelIndex index = applications.index(row, 0);
        const QVariant accentIndex = applications.data(
                index, wam::qmlmodels::ApplicationListModel::AccentIndexRole);
        QCOMPARE(accentIndex.metaType(), QMetaType::fromType<int>());
        QVERIFY(accentIndex.toInt() >= 0);
        QVERIFY(accentIndex.toInt() < 6);

        const QVariant mappedAccentIndex = applications.get(row).value(
                QStringLiteral("accentIndex"));
        QCOMPARE(mappedAccentIndex.metaType(), QMetaType::fromType<int>());
        QCOMPARE(mappedAccentIndex, accentIndex);

        const QVariant iconSource = applications.data(
                index, wam::qmlmodels::ApplicationListModel::IconSourceRole);
        QVERIFY(iconSource.toUrl().isEmpty());
        QVERIFY(applications.get(row).value(QStringLiteral("iconSource"))
                        .toUrl().isEmpty());
    }

    applications.setApplications({
        application(QStringLiteral("alpha"), QStringLiteral("Alpha"),
                    QStringLiteral("Vendor"), QStringLiteral("工具"), 50,
                    RiskLevel::Safe, InstallState::Installed),
        application(QStringLiteral("beta"), QStringLiteral("Beta"),
                    QStringLiteral("Vendor"), QStringLiteral("工具"), 20,
                    RiskLevel::Low, InstallState::Installed),
        application(QStringLiteral("gamma"), QStringLiteral("Gamma"),
                    QStringLiteral("Vendor"), QStringLiteral("工具"), 30,
                    RiskLevel::Unknown, InstallState::Unknown)
    });

    QCOMPARE(applications.indexOfId(QStringLiteral("alpha")), 0);
    QCOMPARE(applications.indexOfId(QStringLiteral("gamma")), 1);
    QCOMPARE(applications.indexOfId(QStringLiteral("beta")), 2);
}

void QmlModelsTest::applicationListMergesScanUpdatesWithoutReset()
{
    using wam::InstallState;
    using wam::RiskLevel;

    wam::qmlmodels::ApplicationListModel applications;
    wam::ApplicationInfo alpha = application(
            QStringLiteral("alpha"), QStringLiteral("Alpha"),
            QStringLiteral("Vendor"), QStringLiteral("工具"), 10,
            RiskLevel::Safe, InstallState::Installed);
    applications.setApplications({alpha});

    QSignalSpy resetSpy(&applications, &QAbstractItemModel::modelReset);
    QSignalSpy insertedSpy(&applications, &QAbstractItemModel::rowsInserted);
    QSignalSpy movedSpy(&applications, &QAbstractItemModel::rowsMoved);
    QSignalSpy layoutSpy(&applications, &QAbstractItemModel::layoutChanged);
    QSignalSpy changedSpy(&applications, &QAbstractItemModel::dataChanged);
    QSignalSpy revisionSpy(
            &applications, &wam::qmlmodels::ApplicationListModel::revisionChanged);

    alpha.totalSize = 50;
    wam::ApplicationInfo beta = application(
            QStringLiteral("beta"), QStringLiteral("Beta"),
            QStringLiteral("Vendor"), QStringLiteral("工具"), 20,
            RiskLevel::Low, InstallState::Installed);
    applications.mergeScanUpdates({alpha, beta});

    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(insertedSpy.count(), 1);
    QCOMPARE(movedSpy.count(), 0);
    QCOMPARE(layoutSpy.count(), 0);
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(revisionSpy.count(), 1);
    QCOMPARE(applications.count(), 2);
    QCOMPARE(applications.get(applications.indexOfId(QStringLiteral("alpha")))
                     .value(QStringLiteral("sizeValue")).toULongLong(),
             50ULL);
    QCOMPARE(applications.maximumSizeValue(), 50.0);
    QCOMPARE(applications.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("alpha"));
    const QVariantMap alphaSummary = applications.getSummary(0);
    QCOMPARE(alphaSummary.value(QStringLiteral("appId")).toString(),
             QStringLiteral("alpha"));
    QCOMPARE(alphaSummary.value(QStringLiteral("sizeValue")).toULongLong(),
             50ULL);
    QVERIFY(!alphaSummary.contains(QStringLiteral("dataGroups")));
    QVERIFY(!alphaSummary.contains(QStringLiteral("evidence")));

    applications.mergeScanUpdates({alpha, beta});
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(insertedSpy.count(), 1);
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(revisionSpy.count(), 1);

    wam::ApplicationInfo gamma = application(
            QStringLiteral("gamma"), QStringLiteral("Gamma"),
            QStringLiteral("Vendor"), QStringLiteral("工具"), 75,
            RiskLevel::Low, InstallState::Installed);
    applications.mergeScanUpdates({gamma});
    QCOMPARE(applications.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("gamma"));
    QCOMPARE(applications.get(1).value(QStringLiteral("appId")).toString(),
             QStringLiteral("alpha"));
    QCOMPARE(applications.get(2).value(QStringLiteral("appId")).toString(),
             QStringLiteral("beta"));

    insertedSpy.clear();
    wam::ApplicationInfo delta = application(
            QStringLiteral("delta"), QStringLiteral("Delta"),
            QStringLiteral("Vendor"), QStringLiteral("工具"), 60,
            RiskLevel::Low, InstallState::Installed);
    wam::ApplicationInfo epsilon = application(
            QStringLiteral("epsilon"), QStringLiteral("Epsilon"),
            QStringLiteral("Vendor"), QStringLiteral("工具"), 5,
            RiskLevel::Low, InstallState::Installed);
    constexpr int upstreamBatchSize = 2;
    applications.mergeScanUpdates({delta, epsilon});
    QCOMPARE(insertedSpy.count(), upstreamBatchSize);
    for (const QList<QVariant> &arguments : insertedSpy) {
        const int first = arguments.at(1).toInt();
        const int last = arguments.at(2).toInt();
        QVERIFY(last >= first);
        QVERIFY(last - first + 1 <= upstreamBatchSize);
    }
    QCOMPARE(layoutSpy.count(), 0);

    movedSpy.clear();
    beta.totalSize = 90;
    applications.mergeScanUpdates({beta});
    QCOMPARE(movedSpy.count(), 1);
    QCOMPARE(layoutSpy.count(), 0);
    QCOMPARE(applications.indexOfId(QStringLiteral("beta")), 0);
    QCOMPARE(applications.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("beta"));
    QCOMPARE(applications.maximumSizeValue(), 90.0);

    beta.totalSize = 1;
    applications.mergeScanUpdates({beta});
    QCOMPARE(movedSpy.count(), 2);
    QCOMPARE(layoutSpy.count(), 0);
    QCOMPARE(applications.indexOfId(QStringLiteral("beta")),
             applications.count() - 1);
    QCOMPARE(applications.maximumSizeValue(), 75.0);
}

void QmlModelsTest::applicationListResolvesStableSelectionAfterSortedMutation()
{
    using wam::InstallState;
    using wam::RiskLevel;

    wam::qmlmodels::ApplicationListModel applications;
    wam::ApplicationInfo selected = application(
            QStringLiteral("selected"), QStringLiteral("Bravo Selected"),
            QStringLiteral("Vendor"), QStringLiteral("工具"), 200,
            RiskLevel::Low, InstallState::Installed);
    applications.setApplications({
        application(QStringLiteral("leader"), QStringLiteral("Alpha Leader"),
                    QStringLiteral("Vendor"), QStringLiteral("工具"), 300,
                    RiskLevel::Low, InstallState::Installed),
        selected,
        application(QStringLiteral("trailer"), QStringLiteral("Delta Trailer"),
                    QStringLiteral("Vendor"), QStringLiteral("工具"), 100,
                    RiskLevel::Low, InstallState::Installed)
    });

    wam::qmlmodels::ApplicationFilterModel filter(&applications);
    filter.setSortMode(1);
    filter.setSortDescending(false);

    const QString selectedAppId = QStringLiteral("selected");
    constexpr int selectedProxyIndex = 1;
    const QVariantMap initialSelection = filter.get(selectedProxyIndex);
    QCOMPARE(initialSelection.value(QStringLiteral("appId")).toString(),
             selectedAppId);
    const int staleNumericIndex = initialSelection.value(
            QStringLiteral("sourceIndex")).toInt();
    QCOMPARE(staleNumericIndex, 1);

    QSignalSpy movedSpy(&applications, &QAbstractItemModel::rowsMoved);
    QSignalSpy insertedSpy(&applications, &QAbstractItemModel::rowsInserted);

    selected.totalSize = 400;
    applications.mergeScanUpdates({selected});
    QCOMPARE(movedSpy.count(), 1);
    QCOMPARE(applications.indexOfId(selectedAppId), 0);
    const QVariantMap movedSelection = filter.get(selectedProxyIndex);
    QCOMPARE(movedSelection.value(QStringLiteral("appId")).toString(),
             selectedAppId);
    QCOMPARE(movedSelection.value(QStringLiteral("sourceIndex")).toInt(), 0);
    QCOMPARE(applications.get(staleNumericIndex)
                     .value(QStringLiteral("appId")).toString(),
             QStringLiteral("leader"));

    applications.mergeScanUpdates({application(
            QStringLiteral("inserted"), QStringLiteral("Echo Inserted"),
            QStringLiteral("Vendor"), QStringLiteral("工具"), 350,
            RiskLevel::Low, InstallState::Installed)});
    QCOMPARE(insertedSpy.count(), 1);

    const int resolvedIndex = applications.indexOfId(selectedAppId);
    QCOMPARE(resolvedIndex, 0);
    const QVariantMap insertedSelection = filter.get(selectedProxyIndex);
    QCOMPARE(insertedSelection.value(QStringLiteral("appId")).toString(),
             selectedAppId);
    QCOMPARE(insertedSelection.value(QStringLiteral("sourceIndex")).toInt(),
             resolvedIndex);
    QCOMPARE(applications.get(staleNumericIndex)
                     .value(QStringLiteral("appId")).toString(),
             QStringLiteral("inserted"));
}

void QmlModelsTest::applicationListExposesIndependentOrphanAssessment()
{
    using wam::AttributionState;
    using wam::InstallState;
    using wam::InstallationState;
    using wam::OwnerKind;
    using wam::RiskLevel;

    wam::ApplicationInfo candidate = application(
            QStringLiteral("candidate"), QStringLiteral("Candidate Data"),
            QStringLiteral("Vendor"), QStringLiteral("工具"), 120,
            RiskLevel::Caution, InstallState::PotentialOrphan);
    candidate.confidence = 91;
    candidate.attribution = {AttributionState::Verified, 91, {}};
    candidate.installation = {InstallationState::Installed, 84, {}};
    candidate.ownerKind = OwnerKind::PackageManager;
    candidate.summary = QStringLiteral("应用归属证据摘要");
    candidate.orphanAssessment.state = InstallState::PotentialOrphan;
    candidate.orphanAssessment.confidence = 86;
    candidate.orphanAssessment.summary = QStringLiteral("潜在残留评估摘要");
    candidate.orphanAssessment.evaluated = true;

    wam::ApplicationInfo blocked = application(
            QStringLiteral("blocked"), QStringLiteral("Blocked Data"),
            QStringLiteral("Vendor"), QStringLiteral("工具"), 80,
            RiskLevel::Unknown, InstallState::Unknown);
    blocked.confidence = 74;
    blocked.attribution = {AttributionState::StrongInferred, 74, {}};
    blocked.installation = {InstallationState::NotObserved, 79, {}};
    blocked.summary = QStringLiteral("目录归属仍需确认");
    blocked.orphanAssessment.state = InstallState::Unknown;
    blocked.orphanAssessment.confidence = 0;
    blocked.orphanAssessment.summary = QStringLiteral("孤儿评估被保守阻断");
    blocked.orphanAssessment.blockingReasons = {
        QStringLiteral("运行进程证据不完整"),
        QStringLiteral("扫描期间存在无法读取的位置")
    };
    blocked.orphanAssessment.evaluated = true;

    wam::ApplicationInfo installed = application(
            QStringLiteral("installed"), QStringLiteral("Installed App"),
            QStringLiteral("Vendor"), QStringLiteral("工具"), 40,
            RiskLevel::Low, InstallState::Installed);
    installed.confidence = 97;
    installed.attribution = {AttributionState::Verified, 97, {}};
    installed.installation = {InstallationState::Installed, 95, {}};
    installed.orphanAssessment.state = InstallState::Installed;
    installed.orphanAssessment.summary = QStringLiteral("存在可信安装证据");
    installed.orphanAssessment.evaluated = true;

    wam::qmlmodels::ApplicationListModel applications;
    applications.setApplications({candidate, blocked, installed});

    const QHash<int, QByteArray> roles = applications.roleNames();
    QCOMPARE(roles.value(
                     wam::qmlmodels::ApplicationListModel::OrphanConfidenceRole),
             QByteArray("orphanConfidence"));
    QCOMPARE(roles.value(
                     wam::qmlmodels::ApplicationListModel::OrphanSummaryRole),
             QByteArray("orphanSummary"));
    QCOMPARE(roles.value(
                     wam::qmlmodels::ApplicationListModel::OrphanBlockingReasonsRole),
             QByteArray("orphanBlockingReasons"));
    QCOMPARE(roles.value(
                     wam::qmlmodels::ApplicationListModel::AttributionStateRole),
             QByteArray("attributionState"));
    QCOMPARE(roles.value(
                     wam::qmlmodels::ApplicationListModel::AttributionConfidenceRole),
             QByteArray("attributionConfidence"));
    QCOMPARE(roles.value(
                     wam::qmlmodels::ApplicationListModel::InstallationStateRole),
             QByteArray("installationState"));
    QCOMPARE(roles.value(
                     wam::qmlmodels::ApplicationListModel::InstallationConfidenceRole),
             QByteArray("installationConfidence"));
    QCOMPARE(roles.value(
                     wam::qmlmodels::ApplicationListModel::OwnerKindRole),
             QByteArray("ownerKind"));
    QCOMPARE(roles.value(
                     wam::qmlmodels::ApplicationListModel::OwnerKindTextRole),
             QByteArray("ownerKindText"));

    const int candidateIndex = applications.indexOfId(QStringLiteral("candidate"));
    const int blockedIndex = applications.indexOfId(QStringLiteral("blocked"));
    QVERIFY(candidateIndex >= 0);
    QVERIFY(blockedIndex >= 0);

    const QModelIndex candidateModelIndex = applications.index(candidateIndex, 0);
    QCOMPARE(applications.data(
                     candidateModelIndex,
                     wam::qmlmodels::ApplicationListModel::ConfidenceRole).toInt(),
             91);
    QCOMPARE(applications.data(
                     candidateModelIndex,
                     wam::qmlmodels::ApplicationListModel::AttributionStateRole).toInt(),
             static_cast<int>(AttributionState::Verified));
    QCOMPARE(applications.data(
                     candidateModelIndex,
                     wam::qmlmodels::ApplicationListModel::AttributionConfidenceRole).toInt(),
             91);
    QCOMPARE(applications.data(
                     candidateModelIndex,
                     wam::qmlmodels::ApplicationListModel::InstallationStateRole).toInt(),
             static_cast<int>(InstallationState::Installed));
    QCOMPARE(applications.data(
                     candidateModelIndex,
                     wam::qmlmodels::ApplicationListModel::InstallationConfidenceRole).toInt(),
             84);
    QCOMPARE(applications.data(
                     candidateModelIndex,
                     wam::qmlmodels::ApplicationListModel::OrphanConfidenceRole).toInt(),
             86);
    QCOMPARE(applications.data(
                     candidateModelIndex,
                     wam::qmlmodels::ApplicationListModel::SummaryRole).toString(),
             QStringLiteral("应用归属证据摘要"));
    QCOMPARE(applications.data(
                     candidateModelIndex,
                     wam::qmlmodels::ApplicationListModel::OrphanSummaryRole).toString(),
             QStringLiteral("潜在残留评估摘要"));

    const QVariantMap candidateMap = applications.get(candidateIndex);
    QCOMPARE(candidateMap.value(QStringLiteral("confidence")).toInt(), 91);
    QCOMPARE(candidateMap.value(QStringLiteral("attributionState")).toInt(),
             static_cast<int>(AttributionState::Verified));
    QCOMPARE(candidateMap.value(QStringLiteral("attributionConfidence")).toInt(), 91);
    QCOMPARE(candidateMap.value(QStringLiteral("installationState")).toInt(),
             static_cast<int>(InstallationState::Installed));
    QCOMPARE(candidateMap.value(QStringLiteral("installationConfidence")).toInt(), 84);
    QCOMPARE(candidateMap.value(QStringLiteral("ownerKind")).toInt(),
             static_cast<int>(OwnerKind::PackageManager));
    QCOMPARE(candidateMap.value(QStringLiteral("ownerKindText")).toString(),
             QStringLiteral("包管理器"));
    QCOMPARE(candidateMap.value(QStringLiteral("orphanConfidence")).toInt(), 86);
    QCOMPARE(candidateMap.value(QStringLiteral("summary")).toString(),
             QStringLiteral("应用归属证据摘要"));
    QCOMPARE(candidateMap.value(QStringLiteral("orphanSummary")).toString(),
             QStringLiteral("潜在残留评估摘要"));

    const QStringList blockingReasons {
        QStringLiteral("运行进程证据不完整"),
        QStringLiteral("扫描期间存在无法读取的位置")
    };
    const QModelIndex blockedModelIndex = applications.index(blockedIndex, 0);
    QCOMPARE(applications.data(
                     blockedModelIndex,
                     wam::qmlmodels::ApplicationListModel::OrphanBlockingReasonsRole)
                     .toStringList(),
             blockingReasons);
    QCOMPARE(applications.get(blockedIndex)
                     .value(QStringLiteral("orphanBlockingReasons"))
                     .toStringList(),
             blockingReasons);

    QCOMPARE(applications.potentialOrphanCount(), 1);
    wam::qmlmodels::ApplicationFilterModel filter(&applications);
    filter.setInstallStateFilter(static_cast<int>(InstallState::PotentialOrphan));
    QCOMPARE(filter.count(), 1);
    const QVariantMap filteredCandidate = filter.get(0);
    QCOMPARE(filteredCandidate.value(QStringLiteral("appId")).toString(),
             QStringLiteral("candidate"));
    QCOMPARE(filteredCandidate.value(QStringLiteral("orphanConfidence")).toInt(), 86);
    QCOMPARE(filteredCandidate.value(QStringLiteral("orphanSummary")).toString(),
             QStringLiteral("潜在残留评估摘要"));
}

void QmlModelsTest::applicationFilterCombinesSearchAndExactFilters()
{
    using wam::InstallState;
    using wam::RiskLevel;

    wam::qmlmodels::ApplicationListModel applications;
    applications.setApplications({
        application(QStringLiteral("alpha"), QStringLiteral("Alpha Editor"),
                    QStringLiteral("Northwind"), QStringLiteral("开发工具"), 120,
                    RiskLevel::Low, InstallState::Installed),
        application(QStringLiteral("browser"), QStringLiteral("Browser Cache"),
                    QStringLiteral("Contoso"), QStringLiteral("浏览器"), 90,
                    RiskLevel::High, InstallState::PotentialOrphan),
        application(QStringLiteral("gamma"), QStringLiteral("Gamma Extension"),
                    QStringLiteral("Contoso"), QStringLiteral("扩展"), 40,
                    RiskLevel::High, InstallState::Installed)
    });

    wam::qmlmodels::ApplicationFilterModel filter(&applications);
    QCOMPARE(filter.count(), 3);

    const int alphaIndex = applications.indexOfId(QStringLiteral("alpha"));
    const int browserIndex = applications.indexOfId(QStringLiteral("browser"));
    const int gammaIndex = applications.indexOfId(QStringLiteral("gamma"));
    QVERIFY(alphaIndex >= 0);
    QVERIFY(browserIndex >= 0);
    QVERIFY(gammaIndex >= 0);
    QVERIFY(filter.containsSourceIndex(alphaIndex));
    QVERIFY(filter.containsSourceIndex(browserIndex));
    QVERIFY(filter.containsSourceIndex(gammaIndex));
    QVERIFY(!filter.containsSourceIndex(-1));
    QVERIFY(!filter.containsSourceIndex(applications.count()));

    filter.setSearchText(QStringLiteral("contoso"));
    QCOMPARE(filter.count(), 2);
    QVERIFY(!filter.containsSourceIndex(alphaIndex));
    QVERIFY(filter.containsSourceIndex(browserIndex));
    QVERIFY(filter.containsSourceIndex(gammaIndex));
    filter.setRiskFilter(static_cast<int>(RiskLevel::High));
    QCOMPARE(filter.count(), 2);
    filter.setInstallStateFilter(static_cast<int>(InstallState::PotentialOrphan));
    QCOMPARE(filter.count(), 1);
    QVERIFY(!filter.containsSourceIndex(alphaIndex));
    QVERIFY(filter.containsSourceIndex(browserIndex));
    QVERIFY(!filter.containsSourceIndex(gammaIndex));
    QCOMPARE(filter.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("browser"));

    filter.setSearchText(QStringLiteral("开发"));
    QCOMPARE(filter.count(), 0);
    filter.setRiskFilter(-1);
    filter.setInstallStateFilter(-1);
    QCOMPARE(filter.count(), 1);
    QVERIFY(filter.containsSourceIndex(alphaIndex));
    QVERIFY(!filter.containsSourceIndex(browserIndex));
    QVERIFY(!filter.containsSourceIndex(gammaIndex));
    QCOMPARE(filter.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("alpha"));

    filter.setRiskFilter(99);
    filter.setInstallStateFilter(-2);
    QCOMPARE(filter.riskFilter(), -1);
    QCOMPARE(filter.installStateFilter(), -1);
}

void QmlModelsTest::applicationFilterSortsAndMapsSourceRows()
{
    using wam::InstallState;
    using wam::RiskLevel;

    wam::qmlmodels::ApplicationListModel applications;
    applications.setApplications({
        application(QStringLiteral("safe"), QStringLiteral("Zulu Safe"),
                    QStringLiteral("Vendor"), QStringLiteral("工具"), 20,
                    RiskLevel::Safe, InstallState::Installed),
        application(QStringLiteral("unknown"), QStringLiteral("Echo Unknown"),
                    QStringLiteral("Vendor"), QStringLiteral("工具"), 60,
                    RiskLevel::Unknown, InstallState::Unknown),
        application(QStringLiteral("protected"), QStringLiteral("Bravo Protected"),
                    QStringLiteral("Vendor"), QStringLiteral("工具"), 80,
                    RiskLevel::Protected, InstallState::Installed),
        application(QStringLiteral("high"), QStringLiteral("Alpha High"),
                    QStringLiteral("Vendor"), QStringLiteral("工具"), 80,
                    RiskLevel::High, InstallState::PotentialOrphan)
    });

    wam::qmlmodels::ApplicationFilterModel filter(&applications);
    QCOMPARE(filter.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("high"));
    QCOMPARE(filter.get(1).value(QStringLiteral("appId")).toString(),
             QStringLiteral("protected"));

    filter.setSortDescending(false);
    QCOMPARE(filter.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("safe"));

    filter.setSortMode(1);
    QCOMPARE(filter.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("high"));
    filter.setSortDescending(true);
    QCOMPARE(filter.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("safe"));

    filter.setSortMode(2);
    QCOMPARE(filter.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("protected"));
    QCOMPARE(filter.get(1).value(QStringLiteral("appId")).toString(),
             QStringLiteral("high"));
    QCOMPARE(filter.get(2).value(QStringLiteral("appId")).toString(),
             QStringLiteral("unknown"));

    for (int proxyIndex = 0; proxyIndex < filter.count(); ++proxyIndex) {
        const QVariantMap item = filter.get(proxyIndex);
        const int sourceIndex = item.value(QStringLiteral("sourceIndex")).toInt();
        QCOMPARE(applications.get(sourceIndex).value(QStringLiteral("appId")),
                 item.value(QStringLiteral("appId")));
    }
}

void QmlModelsTest::scanServicePublishesCompletedTargetUpdates()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString alpha = QDir(temporary.path()).filePath(
            QStringLiteral("Alpha/Cache"));
    const QString beta = QDir(temporary.path()).filePath(
            QStringLiteral("Beta/Logs"));
    QVERIFY(QDir().mkpath(alpha));
    QVERIFY(QDir().mkpath(beta));
    writeFile(QDir(alpha).filePath(QStringLiteral("cache.bin")),
              QByteArray(7, 'a'));
    writeFile(QDir(beta).filePath(QStringLiteral("latest.log")),
              QByteArray(11, 'b'));

    wam::services::ScanService service;
    QSignalSpy updateSpy(
            &service, &wam::services::ScanService::scanUpdatesReady);
    QSignalSpy completedSpy(
            &service, &wam::services::ScanService::scanCompleted);
    QSignalSpy progressSpy(
            &service, &wam::services::ScanService::progressChanged);

    service.startScan({temporary.path()});
    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 5000);

    QVERIFY(!updateSpy.isEmpty());
    QVERIFY(updateSpy.count() <= 2);
    int previousCompletedTargets = 0;
    for (int index = 0; index < updateSpy.size(); ++index) {
        const QList<QVariant> arguments = updateSpy.at(index);
        const QVector<wam::ApplicationInfo> updates =
                qvariant_cast<QVector<wam::ApplicationInfo>>(arguments.at(0));
        QCOMPARE(arguments.at(1).toInt(), 0);
        const int completedTargets = arguments.at(2).toInt();
        QVERIFY(completedTargets > previousCompletedTargets);
        previousCompletedTargets = completedTargets;
        QCOMPARE(arguments.at(3).toInt(), 2);
        QVERIFY(!updates.isEmpty());
        QVERIFY(updates.size() <= completedTargets);
    }

    const wam::ScanResult finalResult = qvariant_cast<wam::ScanResult>(
            completedSpy.constFirst().constFirst());
    QCOMPARE(finalResult.applications.size(), 2);
    QVERIFY(!finalResult.cancelled);

    QVERIFY(!progressSpy.isEmpty());
    int previousProgress = 0;
    for (const QList<QVariant> &arguments : progressSpy) {
        const int progress = arguments.constFirst().toInt();
        const QString path = arguments.at(1).toString();
        QVERIFY(progress >= previousProgress);
        QVERIFY(progress <= 100);
        if (path.isEmpty())
            QCOMPARE(progress, 100);
        previousProgress = progress;
    }
    QCOMPARE(previousProgress, 100);
}

void QmlModelsTest::scanServiceBoundsProgressiveUpdateDelivery()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    constexpr int targetCount = 96;
    for (int index = 0; index < targetCount; ++index) {
        const QString cache = QDir(temporary.path()).filePath(
                QStringLiteral("App-%1/Cache")
                        .arg(index, 3, 10, QLatin1Char('0')));
        QVERIFY(QDir().mkpath(cache));
        writeFile(QDir(cache).filePath(QStringLiteral("cache.bin")),
                  QByteArray(1, 'x'));
    }

    wam::services::ScanService service;
    QSignalSpy updateSpy(
            &service, &wam::services::ScanService::scanUpdatesReady);
    QSignalSpy completedSpy(
            &service, &wam::services::ScanService::scanCompleted);
    QElapsedTimer elapsed;
    elapsed.start();

    service.startScan({temporary.path()});
    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 10000);

    QVERIFY(!updateSpy.isEmpty());
    const int maximumDeliveries =
            static_cast<int>(elapsed.elapsed() / 50) + 2;
    QVERIFY2(updateSpy.count() <= maximumDeliveries,
             qPrintable(QStringLiteral("%1 次更新超过 %2 次节流上限")
                                .arg(updateSpy.count())
                                .arg(maximumDeliveries)));

    const wam::ScanResult finalResult = qvariant_cast<wam::ScanResult>(
            completedSpy.constFirst().constFirst());
    QCOMPARE(finalResult.applications.size(), targetCount);
    QVERIFY(!finalResult.cancelled);
}

void QmlModelsTest::viewModelPublishesBackgroundScan()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString cache = QDir(temporary.path()).filePath(QStringLiteral("示例应用/Cache"));
    const QString credentials = QDir(temporary.path()).filePath(
            QStringLiteral("示例应用/Login Data"));
    QVERIFY(QDir().mkpath(cache));
    QVERIFY(QDir().mkpath(credentials));
    writeFile(QDir(cache).filePath(QStringLiteral("cache.bin")), QByteArray(9, 'c'));
    writeFile(QDir(credentials).filePath(QStringLiteral("account.db")), QByteArray(4, 'd'));

    const QByteArray previousRoot = qgetenv("WAM_SCAN_ROOT");
    qputenv("WAM_SCAN_ROOT", temporary.path().toUtf8());

    wam::qmlmodels::ApplicationListModel applications;
    wam::qmlmodels::ScanViewModel scan(&applications);
    auto *service = scan.findChild<wam::services::ScanService *>();
    QVERIFY(service);
    QSignalSpy revisionSpy(&applications, &wam::qmlmodels::ApplicationListModel::revisionChanged);
    QSignalSpy updateSpy(
            service, &wam::services::ScanService::scanUpdatesReady);
    QSignalSpy completedSpy(
            service, &wam::services::ScanService::scanCompleted);
    QSignalSpy resetSpy(&applications, &QAbstractItemModel::modelReset);
    scan.startScan();
    QCOMPARE(resetSpy.count(), 1);
    resetSpy.clear();
    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!scan.running(), 5000);

    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(updateSpy.count(), 1);
    QVERIFY(revisionSpy.count() >= 2);
    QCOMPARE(scan.progress(), 100);
    QVERIFY(!scan.recentPaths().isEmpty());
    QVERIFY(scan.recentPaths().size() <= 5);
    const QStringList firstRecentPaths = scan.recentPaths();
    QSet<QString> uniqueRecentPaths;
    for (const QString &path : firstRecentPaths)
        uniqueRecentPaths.insert(path);
    QCOMPARE(firstRecentPaths.size(), uniqueRecentPaths.size());
    QVERIFY(std::any_of(firstRecentPaths.cbegin(), firstRecentPaths.cend(),
                        [](const QString &path) {
        return QFileInfo(path).isFile();
    }));
    QCOMPARE(applications.count(), 1);
    const QVariantMap application = applications.get(0);
    QCOMPARE(application.value(QStringLiteral("riskLevel")).toInt(),
             static_cast<int>(wam::RiskLevel::Unknown));
    QCOMPARE(application.value(QStringLiteral("reclaimableText")).toString(),
             QStringLiteral("0 B"));
    QCOMPARE(application.value(QStringLiteral("unknownSizeText")).toString(),
             QStringLiteral("13 B"));

    revisionSpy.clear();
    updateSpy.clear();
    completedSpy.clear();
    scan.startScan();
    QCOMPARE(resetSpy.count(), 1);
    resetSpy.clear();
    QVERIFY(scan.running());
    QVERIFY(scan.recentPaths().isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!scan.running(), 5000);
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(updateSpy.count(), 1);
    QVERIFY(revisionSpy.count() >= 2);
    QVERIFY(!scan.recentPaths().isEmpty());

    if (previousRoot.isNull())
        qunsetenv("WAM_SCAN_ROOT");
    else
        qputenv("WAM_SCAN_ROOT", previousRoot);
}

void QmlModelsTest::viewModelBatchesLargeScanUpdates()
{
    wam::qmlmodels::ApplicationListModel applications;
    wam::qmlmodels::ScanViewModel scan(&applications);
    auto *service = scan.findChild<wam::services::ScanService *>();
    QVERIFY(service);

    QSignalSpy resetSpy(&applications, &QAbstractItemModel::modelReset);
    QSignalSpy insertedSpy(&applications, &QAbstractItemModel::rowsInserted);
    QSignalSpy acceptedSpy(
            &scan, &wam::qmlmodels::ScanViewModel::scanResultAccepted);

    QVector<wam::ApplicationInfo> updates;
    constexpr int applicationCount = 80;
    updates.reserve(applicationCount);
    for (int index = 0; index < applicationCount; ++index) {
        updates.append(application(
                QStringLiteral("app-%1").arg(index, 3, 10, QLatin1Char('0')),
                QStringLiteral("Application %1").arg(index),
                QStringLiteral("Vendor"), QStringLiteral("工具"),
                static_cast<quint64>(index + 1),
                wam::RiskLevel::Low, wam::InstallState::Installed));
    }
    updates.first().cleanupCandidates.append(wam::CleanupCandidateInfo {});

    service->scanStarted();
    resetSpy.clear();
    service->scanUpdatesReady(updates, 0, applicationCount, applicationCount);

    QTRY_VERIFY_WITH_TIMEOUT(!insertedSpy.isEmpty(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(applications.count(), applicationCount, 2000);
    QCOMPARE(applications.get(0).value(QStringLiteral("sizeValue")).toULongLong(),
             static_cast<quint64>(applicationCount));
    QCOMPARE(applications.get(applicationCount - 1)
                     .value(QStringLiteral("sizeValue")).toULongLong(),
             1ULL);
    const int firstApplicationIndex = applications.indexOfId(
            QStringLiteral("app-000"));
    QVERIFY(firstApplicationIndex >= 0);
    QVERIFY(applications.applications()
                    .at(firstApplicationIndex).cleanupCandidates.isEmpty());
    QCOMPARE(resetSpy.count(), 0);
    QVERIFY(insertedSpy.size() >= applicationCount / 4);
    for (const QList<QVariant> &arguments : insertedSpy) {
        const int first = arguments.at(1).toInt();
        const int last = arguments.at(2).toInt();
        QVERIFY(last >= first);
        QVERIFY(last - first + 1 <= 4);
    }

    wam::ScanResult finalResult;
    finalResult.applications = updates;
    service->scanCompleted(finalResult);
    QTRY_VERIFY_WITH_TIMEOUT(!scan.running(), 2000);
    QCOMPARE(applications.count(), applicationCount);
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(acceptedSpy.count(), 1);
    const wam::ScanResult acceptedResult = qvariant_cast<wam::ScanResult>(
            acceptedSpy.constFirst().constFirst());
    QCOMPARE(acceptedResult.applications.constFirst().cleanupCandidates.size(), 1);
}

void QmlModelsTest::viewModelKeepsInterleavedProgressMonotonic()
{
    wam::qmlmodels::ApplicationListModel applications;
    wam::qmlmodels::ScanViewModel scan(&applications);
    auto *service = scan.findChild<wam::services::ScanService *>();
    QVERIFY(service);

    QVector<int> observedProgress;
    connect(&scan, &wam::qmlmodels::ScanViewModel::progressChanged,
            &scan, [&scan, &observedProgress] {
        observedProgress.append(scan.progress());
    });
    service->scanStarted();
    service->scanUpdatesReady({}, 0, 96, 100);
    QCOMPARE(scan.progress(), 96);

    service->progressChanged(2, QStringLiteral("C:/扫描/较早路径"));
    QCOMPARE(scan.progress(), 96);
    QCOMPARE(scan.currentPath(), QDir::toNativeSeparators(
                                      QStringLiteral("C:/扫描/较早路径")));

    service->progressChanged(97, QStringLiteral("C:/扫描/当前路径"));
    QCOMPARE(scan.progress(), 97);
    service->scanUpdatesReady({}, 0, 90, 100);
    QCOMPARE(scan.progress(), 97);
    service->progressChanged(100, {});
    QCOMPARE(scan.progress(), 100);
    QCOMPARE(scan.currentPath(), QDir::toNativeSeparators(
                                      QStringLiteral("C:/扫描/当前路径")));

    int previousProgress = 0;
    for (const int progress : observedProgress) {
        QVERIFY(progress >= previousProgress);
        previousProgress = progress;
    }

    service->scanCompleted({});
    QTRY_VERIFY_WITH_TIMEOUT(!scan.running(), 2000);
    QVERIFY(scan.currentPath().isEmpty());
}

void QmlModelsTest::viewModelPreservesAcceptedIssuesAcrossInterruptedScan()
{
    wam::qmlmodels::ApplicationListModel applications;
    wam::qmlmodels::ScanViewModel scan(&applications);
    auto *service = scan.findChild<wam::services::ScanService *>();
    QVERIFY(service);

    wam::ScanResult accepted;
    accepted.applications.append(application(
            QStringLiteral("accepted"), QStringLiteral("Accepted App"),
            QStringLiteral("Vendor"), QStringLiteral("工具"), 42,
            wam::RiskLevel::Low, wam::InstallState::Installed));
    accepted.issues.append(wam::ScanIssue {});
    accepted.issues.append(wam::ScanIssue {});

    service->scanStarted();
    service->scanCompleted(accepted);
    QTRY_VERIFY_WITH_TIMEOUT(!scan.running(), 2000);
    QCOMPARE(scan.issueCount(), 2);
    QVERIFY(scan.partialResult());
    QCOMPARE(applications.count(), 1);
    QCOMPARE(applications.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("accepted"));

    QSignalSpy acceptedSpy(&scan, &wam::qmlmodels::ScanViewModel::scanResultAccepted);
    wam::ScanResult incremental;
    incremental.applications.append(application(
            QStringLiteral("incremental"), QStringLiteral("Incremental App"),
            QStringLiteral("Vendor"), QStringLiteral("工具"), 128,
            wam::RiskLevel::Unknown, wam::InstallState::Unknown));
    for (int index = 0; index < 4; ++index)
        incremental.issues.append(wam::ScanIssue {});

    wam::ScanResult cancelled;
    cancelled.cancelled = true;
    for (int index = 0; index < 5; ++index)
        cancelled.issues.append(wam::ScanIssue {});

    service->scanStarted();
    QCOMPARE(applications.count(), 0);
    QCOMPARE(scan.issueCount(), 0);
    service->scanCompleted(cancelled);
    QCOMPARE(applications.count(), 1);
    QCOMPARE(applications.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("accepted"));
    QCOMPARE(scan.issueCount(), 2);
    QCOMPARE(acceptedSpy.count(), 0);

    service->scanStarted();
    QCOMPARE(applications.count(), 0);
    QCOMPARE(scan.issueCount(), 0);
    service->scanUpdatesReady(incremental.applications,
                              incremental.issues.size(), 1, 3);
    QTRY_COMPARE_WITH_TIMEOUT(applications.count(), 1, 2000);
    QCOMPARE(applications.count(), 1);
    QCOMPARE(applications.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("incremental"));
    QCOMPARE(scan.issueCount(), 4);
    QCOMPARE(scan.progress(), 33);
    QCOMPARE(acceptedSpy.count(), 0);
    service->scanCompleted(cancelled);
    QCOMPARE(scan.issueCount(), 2);
    QVERIFY(scan.partialResult());
    QCOMPARE(applications.count(), 1);
    QCOMPARE(applications.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("accepted"));
    QCOMPARE(applications.get(0).value(QStringLiteral("sizeValue")).toULongLong(),
             42ULL);
    QCOMPARE(acceptedSpy.count(), 0);
    QCOMPARE(scan.statusText(), QStringLiteral("扫描已取消，保留上一次完整结果"));

    service->scanStarted();
    QCOMPARE(scan.issueCount(), 0);
    service->scanUpdatesReady(incremental.applications,
                              incremental.issues.size(), 1, 2);
    QTRY_COMPARE_WITH_TIMEOUT(applications.count(), 1, 2000);
    QCOMPARE(applications.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("incremental"));
    QCOMPARE(acceptedSpy.count(), 0);
    service->scanFailed(QStringLiteral("扫描未能完成"),
                        QStringLiteral("测试错误"));
    QCOMPARE(scan.issueCount(), 2);
    QVERIFY(scan.partialResult());
    QCOMPARE(applications.count(), 1);
    QCOMPARE(applications.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("accepted"));
    QCOMPARE(acceptedSpy.count(), 0);
    QCOMPARE(scan.errorMessage(), QStringLiteral("扫描未能完成"));

    service->scanStarted();
    service->scanUpdatesReady(incremental.applications,
                              incremental.issues.size(), 1, 1);
    QCOMPARE(acceptedSpy.count(), 0);
    service->scanCompleted(incremental);
    QTRY_COMPARE_WITH_TIMEOUT(acceptedSpy.count(), 1, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(!scan.running(), 2000);
    QCOMPARE(applications.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("incremental"));
    QCOMPARE(scan.issueCount(), 4);
}

void QmlModelsTest::viewModelAppliesBuiltInRules()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString roaming = QDir(temporary.path()).filePath(QStringLiteral("Roaming"));
    const QString cache = QDir(roaming).filePath(QStringLiteral("discord/Cache"));
    const QString session = QDir(roaming).filePath(
            QStringLiteral("discord/Session Storage"));
    QVERIFY(QDir().mkpath(cache));
    QVERIFY(QDir().mkpath(session));
    writeFile(QDir(cache).filePath(QStringLiteral("cache.bin")), QByteArray(9, 'c'));
    writeFile(QDir(session).filePath(QStringLiteral("session.bin")), QByteArray(4, 's'));

    const QByteArray previousRoot = qgetenv("WAM_SCAN_ROOT");
    qputenv("WAM_SCAN_ROOT", roaming.toUtf8());

    wam::qmlmodels::ApplicationListModel applications;
    wam::qmlmodels::ScanViewModel scan(&applications);
    auto *service = scan.findChild<wam::services::ScanService *>();
    QVERIFY(service);
    QSignalSpy completedSpy(
            service, &wam::services::ScanService::scanCompleted);
    scan.startScan();
    QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!scan.running(), 5000);

    QCOMPARE(applications.count(), 1);
    const QVariantMap application = applications.get(0);
    QCOMPARE(application.value(QStringLiteral("appId")).toString(),
             QStringLiteral("discord"));
    QCOMPARE(application.value(QStringLiteral("riskLevel")).toInt(),
             static_cast<int>(wam::RiskLevel::High));
    QCOMPARE(application.value(QStringLiteral("reclaimableText")).toString(),
             QStringLiteral("9 B"));
    QCOMPARE(application.value(QStringLiteral("unknownSizeText")).toString(),
             QStringLiteral("0 B"));

    const QVariantList groups = application.value(QStringLiteral("dataGroups")).toList();
    const auto cacheGroup = std::find_if(groups.cbegin(), groups.cend(), [](const QVariant &value) {
        return value.toMap().value(QStringLiteral("groupId")).toString()
                == QStringLiteral("cache");
    });
    QVERIFY(cacheGroup != groups.cend());
    QCOMPARE(cacheGroup->toMap().value(QStringLiteral("riskLevel")).toInt(),
             static_cast<int>(wam::RiskLevel::Safe));
    QVERIFY(cacheGroup->toMap().value(QStringLiteral("ruleSource")).toString()
                    .contains(QStringLiteral("内置规则 / discord@1")));

    if (previousRoot.isNull())
        qunsetenv("WAM_SCAN_ROOT");
    else
        qputenv("WAM_SCAN_ROOT", previousRoot);
}

QTEST_GUILESS_MAIN(QmlModelsTest)

#include "tst_qml_models.moc"
