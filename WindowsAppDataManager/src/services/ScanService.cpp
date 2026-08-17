#include "ScanService.h"
#include "InstallationEvidenceCollector.h"

#include "../core/classifier/DataClassifier.h"
#include "../core/classifier/RiskAssessment.h"
#include "../core/resolver/AppResolver.h"
#include "../core/resolver/OrphanDetector.h"
#include "../core/scanner/DirectoryScanner.h"
#include "../platform/windows/filesystem/AppDataPaths.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QMetaObject>
#include <QtConcurrentRun>

#include <algorithm>
#include <chrono>
#include <exception>

namespace wam::services {
namespace {

QString displayTarget(const QStringList &roots)
{
    if (roots.isEmpty())
        return QStringLiteral("未找到可扫描的 AppData 目录");
    if (roots.size() == 1)
        return QDir::toNativeSeparators(roots.constFirst());

    const QFileInfo first(roots.constFirst());
    const QString name = first.fileName().toLower();
    if (name == QStringLiteral("local") || name == QStringLiteral("roaming")
            || name == QStringLiteral("locallow")) {
        return QDir::toNativeSeparators(first.dir().absolutePath());
    }
    return QStringLiteral("%1 个 AppData 扫描范围").arg(roots.size());
}

QString pathToQString(const std::filesystem::path &path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

void mergeDataGroup(QVector<DataGroupInfo> &groups, const DataGroupInfo &incoming)
{
    const auto iterator = std::find_if(groups.begin(), groups.end(), [&incoming](const auto &group) {
        return group.id == incoming.id;
    });
    if (iterator == groups.end()) {
        groups.append(incoming);
        return;
    }

    iterator->size += incoming.size;
    iterator->fileCount += incoming.fileCount;
    if (iterator->path.isEmpty())
        iterator->path = incoming.path;
}

void mergeApplication(ApplicationInfo &application, ApplicationInfo incoming)
{
    application.totalSize += incoming.totalSize;
    application.fileCount += incoming.fileCount;
    application.reclaimableSize += incoming.reclaimableSize;
    application.protectedSize += incoming.protectedSize;
    application.unknownSize += incoming.unknownSize;
    application.scanComplete = application.scanComplete && incoming.scanComplete;
    if (incoming.lastModified > application.lastModified)
        application.lastModified = incoming.lastModified;
    application.confidence = std::max(application.confidence, incoming.confidence);
    if (incoming.installState == InstallState::Installed)
        application.installState = InstallState::Installed;

    if (!incoming.location.isEmpty() && !application.location.contains(incoming.location)) {
        if (!application.location.isEmpty())
            application.location += QStringLiteral(" · ");
        application.location += incoming.location;
    }

    for (const DataGroupInfo &group : incoming.dataGroups)
        mergeDataGroup(application.dataGroups, group);

    for (const EvidenceInfo &evidence : incoming.evidence) {
        const bool alreadyPresent = std::any_of(
                application.evidence.cbegin(), application.evidence.cend(),
                [&evidence](const EvidenceInfo &existing) {
            return existing.source == evidence.source
                    && existing.status == evidence.status
                    && existing.detail == evidence.detail;
        });
        if (!alreadyPresent)
            application.evidence.append(evidence);
    }
}

ApplicationInfo scanTarget(const core::ScanTarget &target,
                           const std::atomic_bool &cancelRequested,
                           const core::DirectoryScanner::StatusCallback &statusCallback,
                           QVector<ScanIssue> &issues)
{
    ApplicationInfo application = target.application;
    core::DirectoryScanner scanner;
    core::DataClassifier classifier;
    QHash<QString, int> groupIndexes;

    const auto visitor = [&](const std::filesystem::path &relativePath,
                             quint64 size,
                             qint64) {
        core::Classification classification = classifier.classify(
                relativePath, target.classificationRules, target.ruleSource);
        if (application.confidence < 50) {
            classification.risk = RiskLevel::Unknown;
            classification.rebuildable = RebuildableState::Unknown;
            classification.impact = QStringLiteral(
                    "目录名称提示了可能的数据类型，但应用归属证据不足，不能据此处理。");
            classification.ruleSource = QStringLiteral("启发式 / 低置信度归属");
        }
        auto iterator = groupIndexes.constFind(classification.id);
        if (iterator == groupIndexes.cend()) {
            DataGroupInfo group;
            group.id = classification.id;
            group.category = classification.category;
            group.risk = classification.risk;
            group.rebuildable = classification.rebuildable;
            group.impact = classification.impact;
            group.ruleSource = classification.ruleSource;
            const QString parentPath = pathToQString(relativePath.parent_path());
            group.path = QDir::toNativeSeparators(
                    parentPath.isEmpty() ? target.path : QDir(target.path).filePath(parentPath));
            application.dataGroups.append(std::move(group));
            const int newIndex = application.dataGroups.size() - 1;
            groupIndexes.insert(classification.id, newIndex);
            iterator = groupIndexes.constFind(classification.id);
        }

        DataGroupInfo &group = application.dataGroups[*iterator];
        group.size += size;
        ++group.fileCount;
    };

    const bool verifyStability = target.locationOwnership
                    == RuleLocationOwnership::Exclusive
            && target.application.installState != InstallState::Installed
            && target.locationDiscoveryComplete;
    const core::DirectoryScanStats stats = scanner.scan(
            target.path, cancelRequested, visitor, statusCallback,
            target.excludedPaths, verifyStability);
    application.totalSize = stats.totalSize;
    application.fileCount = stats.fileCount;
    application.scanComplete = target.locationDiscoveryComplete
            && !stats.cancelled && stats.issues.isEmpty()
            && (!verifyStability || stats.stabilityVerified);
    if (stats.latestModifiedMilliseconds > 0) {
        application.lastModified = QDateTime::fromMSecsSinceEpoch(
                stats.latestModifiedMilliseconds);
    }
    issues += stats.issues;

    for (const DataGroupInfo &group : application.dataGroups) {
        if (application.confidence >= 50
                && group.rebuildable == RebuildableState::Yes
                && (group.risk == RiskLevel::Safe || group.risk == RiskLevel::Low)) {
            application.reclaimableSize += group.size;
        }
        if (group.risk == RiskLevel::Protected)
            application.protectedSize += group.size;
        if (group.risk == RiskLevel::Unknown)
            application.unknownSize += group.size;
    }

    std::sort(application.dataGroups.begin(), application.dataGroups.end(),
              [](const auto &left, const auto &right) { return left.size > right.size; });
    application.risk = core::applicationRisk(application);
    if (application.confidence < 50) {
        application.summary = QStringLiteral(
                "缺少足够的应用归属证据，Unknown 数据不会自动进入清理计划。");
    } else if (application.protectedSize > 0 || application.unknownSize > 0) {
        application.summary = QStringLiteral(
                "检测到可重新生成内容以及需要保护或继续识别的数据，必须按分类处理。");
    } else {
        application.summary = QStringLiteral(
                "已按数据类型完成聚合，执行任何操作前仍需重新验证路径和应用状态。");
    }
    return application;
}

ScanResult performScan(const QStringList &roots,
                       const std::shared_ptr<std::atomic_bool> &cancelRequested,
                       const std::function<void(int, const QString &)> &progressCallback)
{
    QElapsedTimer timer;
    timer.start();

    ScanResult result;
    result.roots = roots;
    const core::rules::RuleCatalog &catalog = core::rules::RuleCatalog::builtIn();
    core::AppResolver resolver(catalog, InstallationEvidenceCollector::collect(catalog));
    const QVector<core::ScanTarget> targets = resolver.discoverTargets(roots);
    QHash<QString, int> applicationIndexes;
    QHash<QString, bool> exclusiveLocations;

    for (qsizetype targetIndex = 0; targetIndex < targets.size(); ++targetIndex) {
        if (cancelRequested->load(std::memory_order_relaxed)) {
            result.cancelled = true;
            break;
        }

        const core::ScanTarget &target = targets[targetIndex];
        const int baseProgress = targets.isEmpty()
                ? 0 : static_cast<int>(targetIndex * 100 / targets.size());
        progressCallback(baseProgress, target.path);

        auto lastStatusUpdate = std::chrono::steady_clock::now();
        const auto statusCallback = [&](const QString &path, quint64) {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastStatusUpdate < std::chrono::milliseconds(100))
                return;
            lastStatusUpdate = now;
            progressCallback(baseProgress, path);
        };

        ApplicationInfo application = scanTarget(
                target, *cancelRequested, statusCallback, result.issues);
        auto existing = applicationIndexes.constFind(application.id);
        if (existing == applicationIndexes.cend()) {
            applicationIndexes.insert(application.id, result.applications.size());
            exclusiveLocations.insert(
                    application.id,
                    target.locationOwnership == RuleLocationOwnership::Exclusive);
            result.applications.append(std::move(application));
        } else {
            exclusiveLocations[application.id] = exclusiveLocations.value(application.id)
                    && target.locationOwnership == RuleLocationOwnership::Exclusive;
            mergeApplication(result.applications[*existing], std::move(application));
        }
    }

    result.cancelled = result.cancelled
            || cancelRequested->load(std::memory_order_relaxed);
    for (ApplicationInfo &application : result.applications) {
        application.risk = core::applicationRisk(application);
        core::OrphanDetectionContext orphanContext;
        orphanContext.exclusiveLocations = exclusiveLocations.value(application.id, false);
        orphanContext.scanCompleted = !result.cancelled;
        orphanContext.assessedAt = QDateTime::currentDateTimeUtc();
        application.orphanAssessment = core::OrphanDetector::assess(
                application, orphanContext);
        application.installState = application.orphanAssessment.state;
        result.totalSize += application.totalSize;
        result.fileCount += application.fileCount;
    }
    std::sort(result.applications.begin(), result.applications.end(),
              [](const auto &left, const auto &right) { return left.totalSize > right.totalSize; });

    result.elapsedMilliseconds = timer.elapsed();
    progressCallback(result.cancelled ? 0 : 100, QString());
    return result;
}

} // namespace

ScanService::ScanService(QObject *parent)
    : QObject(parent),
      m_targetPath(displayTarget(platform::windows::AppDataPaths::roots()))
{
    // 在主线程完成静态规则资源注册，后台扫描只读取不可变目录。
    (void)core::rules::RuleCatalog::builtIn();

    connect(&m_watcher, &QFutureWatcher<ScanResult>::finished, this, [this] {
        try {
            emit scanCompleted(m_watcher.result());
        } catch (const std::exception &exception) {
            emit scanFailed(QStringLiteral("扫描未能完成"),
                            QString::fromUtf8(exception.what()));
        } catch (...) {
            emit scanFailed(QStringLiteral("扫描未能完成"),
                            QStringLiteral("未知后台任务异常"));
        }
    });
}

ScanService::~ScanService()
{
    cancelScan();
    m_watcher.waitForFinished();
}

bool ScanService::isRunning() const
{
    return m_watcher.isRunning();
}

QString ScanService::targetPath() const
{
    return m_targetPath;
}

void ScanService::startScan()
{
    if (isRunning())
        return;
    startScan(platform::windows::AppDataPaths::roots());
}

void ScanService::startScan(const QStringList &roots)
{
    if (isRunning())
        return;
    setTargetPath(displayTarget(roots));
    if (roots.isEmpty()) {
        emit scanFailed(QStringLiteral("找不到 AppData 目录"),
                        QStringLiteral("LOCALAPPDATA / APPDATA / LocalLow 均不可用"));
        return;
    }

    m_cancelRequested = std::make_shared<std::atomic_bool>(false);
    const auto progressCallback = [this](int progress, const QString &path) {
        QMetaObject::invokeMethod(this, [this, progress, path] {
            emit progressChanged(progress, path);
        }, Qt::QueuedConnection);
    };

    emit scanStarted();
    m_watcher.setFuture(QtConcurrent::run(
            [roots, cancelRequested = m_cancelRequested, progressCallback] {
                return performScan(roots, cancelRequested, progressCallback);
            }));
}

void ScanService::cancelScan()
{
    if (m_cancelRequested)
        m_cancelRequested->store(true, std::memory_order_relaxed);
}

void ScanService::setTargetPath(QString targetPath)
{
    if (m_targetPath == targetPath)
        return;
    m_targetPath = std::move(targetPath);
    emit targetPathChanged();
}

} // namespace wam::services
