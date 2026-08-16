#include "src/core/classifier/DataClassifier.h"
#include "src/core/classifier/RiskAssessment.h"
#include "src/core/resolver/AppResolver.h"
#include "src/core/scanner/DirectoryScanner.h"
#include "src/qmlmodels/ApplicationFilterModel.h"
#include "src/qmlmodels/ApplicationListModel.h"
#include "src/qmlmodels/ScanViewModel.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <filesystem>
#include <utility>

namespace {

void writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write(contents), contents.size());
}

std::filesystem::path fsPath(const QString &path)
{
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
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

class BackendTest final : public QObject {
    Q_OBJECT

private slots:
    void unknownRemainsUnknown();
    void safeRulesRequireExactPathEvidence();
    void sensitiveRulesTakePriority();
    void applicationRiskPreservesSixLevels();
    void scannerHandlesUnicodeAndCountsFiles();
    void scannerHonorsCancellationAndExclusions();
    void resolverProducesStableCollisionFreeIds();
    void resolverExcludesNestedKnownTargets();
    void applicationFilterCombinesSearchAndExactFilters();
    void applicationFilterSortsAndMapsSourceRows();
    void viewModelPublishesBackgroundScan();
};

void BackendTest::unknownRemainsUnknown()
{
    const wam::core::Classification result =
            wam::core::DataClassifier().classify(fsPath(QStringLiteral("opaque/data.bin")));
    QCOMPARE(result.category, wam::DataCategory::Unknown);
    QCOMPARE(result.risk, wam::RiskLevel::Unknown);
    QCOMPARE(result.rebuildable, wam::RebuildableState::Unknown);
}

void BackendTest::safeRulesRequireExactPathEvidence()
{
    wam::core::DataClassifier classifier;
    QCOMPARE(classifier.classify(fsPath(QStringLiteral("Templates/resume.docx"))).risk,
             wam::RiskLevel::Unknown);
    QCOMPARE(classifier.classify(fsPath(QStringLiteral("catalog/data.bin"))).risk,
             wam::RiskLevel::Unknown);
    QCOMPARE(classifier.classify(fsPath(QStringLiteral("Cache/entry.bin"))).risk,
             wam::RiskLevel::Safe);
    QCOMPARE(classifier.classify(fsPath(QStringLiteral("output/app.log"))).risk,
             wam::RiskLevel::Low);
}

void BackendTest::sensitiveRulesTakePriority()
{
    wam::core::DataClassifier classifier;
    QCOMPARE(classifier.classify(fsPath(QStringLiteral("Cache/Login Data"))).risk,
             wam::RiskLevel::Protected);
    QCOMPARE(classifier.classify(fsPath(QStringLiteral("Cache/Session Storage/item"))).risk,
             wam::RiskLevel::High);
    QCOMPARE(classifier.classify(fsPath(QStringLiteral("Cache/state.sqlite"))).risk,
             wam::RiskLevel::High);
}

void BackendTest::applicationRiskPreservesSixLevels()
{
    wam::ApplicationInfo application;
    application.confidence = 90;
    application.dataGroups = {{.risk = wam::RiskLevel::Safe}};
    QCOMPARE(wam::core::applicationRisk(application), wam::RiskLevel::Safe);

    application.dataGroups.append({.risk = wam::RiskLevel::Unknown});
    QCOMPARE(wam::core::applicationRisk(application), wam::RiskLevel::Unknown);
    application.dataGroups.append({.risk = wam::RiskLevel::High});
    QCOMPARE(wam::core::applicationRisk(application), wam::RiskLevel::High);
    application.dataGroups.append({.risk = wam::RiskLevel::Protected});
    QCOMPARE(wam::core::applicationRisk(application), wam::RiskLevel::Protected);

    application.confidence = 20;
    QCOMPARE(wam::core::applicationRisk(application), wam::RiskLevel::Unknown);
}

void BackendTest::scannerHandlesUnicodeAndCountsFiles()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString unicodeDirectory = QDir(temporary.path()).filePath(QStringLiteral("应用数据/缓存"));
    QVERIFY(QDir().mkpath(unicodeDirectory));
    writeFile(QDir(unicodeDirectory).filePath(QStringLiteral("文件一.bin")), QByteArray(7, 'a'));
    writeFile(QDir(temporary.path()).filePath(QStringLiteral("root.bin")), QByteArray(5, 'b'));

    std::atomic_bool cancelled = false;
    QStringList visited;
    const auto stats = wam::core::DirectoryScanner().scan(
            temporary.path(), cancelled,
            [&visited](const auto &path, quint64, qint64) {
#ifdef _WIN32
                visited.append(QString::fromStdWString(path.generic_wstring()));
#else
                visited.append(QString::fromStdString(path.generic_string()));
#endif
            }, {});

    QCOMPARE(stats.fileCount, quint64(2));
    QCOMPARE(stats.totalSize, quint64(12));
    QVERIFY(std::any_of(visited.cbegin(), visited.cend(), [](const QString &path) {
        return path.contains(QStringLiteral("文件一.bin"));
    }));
}

void BackendTest::scannerHonorsCancellationAndExclusions()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString excluded = QDir(temporary.path()).filePath(QStringLiteral("Known/Data"));
    QVERIFY(QDir().mkpath(excluded));
    writeFile(QDir(excluded).filePath(QStringLiteral("excluded.bin")), QByteArray(10, 'x'));
    writeFile(QDir(temporary.path()).filePath(QStringLiteral("included.bin")), QByteArray(3, 'y'));

    std::atomic_bool cancelled = false;
    const auto stats = wam::core::DirectoryScanner().scan(
            temporary.path(), cancelled, {}, {}, {excluded});
    QCOMPARE(stats.fileCount, quint64(1));
    QCOMPARE(stats.totalSize, quint64(3));

    cancelled.store(true);
    const auto cancelledStats = wam::core::DirectoryScanner().scan(
            temporary.path(), cancelled, {}, {});
    QVERIFY(cancelledStats.cancelled);
    QCOMPARE(cancelledStats.fileCount, quint64(0));
}

void BackendTest::resolverProducesStableCollisionFreeIds()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QVERIFY(QDir().mkpath(QDir(temporary.path()).filePath(QStringLiteral("微信"))));
    QVERIFY(QDir().mkpath(QDir(temporary.path()).filePath(QStringLiteral("网易"))));

    const auto first = wam::core::AppResolver().discoverTargets({temporary.path()});
    const auto second = wam::core::AppResolver().discoverTargets({temporary.path()});
    QCOMPARE(first.size(), 2);
    QCOMPARE(second.size(), 2);
    QVERIFY(first[0].application.id != first[1].application.id);
    QCOMPARE(first[0].application.id, second[0].application.id);
    QCOMPARE(first[1].application.id, second[1].application.id);
}

void BackendTest::resolverExcludesNestedKnownTargets()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString local = QDir(temporary.path()).filePath(QStringLiteral("Local"));
    const QString chrome = QDir(local).filePath(QStringLiteral("Google/Chrome/User Data"));
    const QString drive = QDir(local).filePath(QStringLiteral("Google/DriveFS"));
    QVERIFY(QDir().mkpath(chrome));
    QVERIFY(QDir().mkpath(drive));

    const auto targets = wam::core::AppResolver().discoverTargets({local});
    const auto google = std::find_if(targets.cbegin(), targets.cend(), [](const auto &target) {
        return target.application.name.compare(QStringLiteral("Google"), Qt::CaseInsensitive) == 0;
    });
    QVERIFY(google != targets.cend());
    QVERIFY(!google->excludedPaths.isEmpty());
    QVERIFY(std::any_of(google->excludedPaths.cbegin(), google->excludedPaths.cend(),
                        [](const QString &path) {
        return path.contains(QStringLiteral("chrome"), Qt::CaseInsensitive);
    }));
}

void BackendTest::applicationFilterCombinesSearchAndExactFilters()
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

    filter.setSearchText(QStringLiteral("contoso"));
    QCOMPARE(filter.count(), 2);
    filter.setRiskFilter(static_cast<int>(RiskLevel::High));
    QCOMPARE(filter.count(), 2);
    filter.setInstallStateFilter(static_cast<int>(InstallState::PotentialOrphan));
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("browser"));

    filter.setSearchText(QStringLiteral("开发"));
    QCOMPARE(filter.count(), 0);
    filter.setRiskFilter(-1);
    filter.setInstallStateFilter(-1);
    QCOMPARE(filter.count(), 1);
    QCOMPARE(filter.get(0).value(QStringLiteral("appId")).toString(),
             QStringLiteral("alpha"));

    filter.setRiskFilter(99);
    filter.setInstallStateFilter(-2);
    QCOMPARE(filter.riskFilter(), -1);
    QCOMPARE(filter.installStateFilter(), -1);
}

void BackendTest::applicationFilterSortsAndMapsSourceRows()
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

void BackendTest::viewModelPublishesBackgroundScan()
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

QTEST_GUILESS_MAIN(BackendTest)

#include "tst_backend.moc"
