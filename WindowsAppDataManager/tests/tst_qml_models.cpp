#include "src/qmlmodels/ApplicationFilterModel.h"
#include "src/qmlmodels/ApplicationListModel.h"
#include "src/qmlmodels/ScanViewModel.h"

#include <QDir>
#include <QFile>
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
    void applicationFilterCombinesSearchAndExactFilters();
    void applicationFilterSortsAndMapsSourceRows();
    void viewModelPublishesBackgroundScan();
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
    QSignalSpy revisionSpy(&applications, &wam::qmlmodels::ApplicationListModel::revisionChanged);
    scan.startScan();
    QVERIFY(revisionSpy.wait(5000));

    QVERIFY(!scan.running());
    QCOMPARE(scan.progress(), 100);
    QCOMPARE(applications.count(), 1);
    const QVariantMap application = applications.get(0);
    QCOMPARE(application.value(QStringLiteral("riskLevel")).toInt(),
             static_cast<int>(wam::RiskLevel::Unknown));
    QCOMPARE(application.value(QStringLiteral("reclaimableText")).toString(),
             QStringLiteral("0 B"));
    QCOMPARE(application.value(QStringLiteral("unknownSizeText")).toString(),
             QStringLiteral("13 B"));

    if (previousRoot.isNull())
        qunsetenv("WAM_SCAN_ROOT");
    else
        qputenv("WAM_SCAN_ROOT", previousRoot);
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
    QSignalSpy revisionSpy(&applications, &wam::qmlmodels::ApplicationListModel::revisionChanged);
    scan.startScan();
    QVERIFY(revisionSpy.wait(5000));

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
