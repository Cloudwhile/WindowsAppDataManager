#include "OrphanDetector.h"

#include <algorithm>

namespace wam::core {
namespace {

QString sourceName(EvidenceSource source)
{
    switch (source) {
    case EvidenceSource::Registry: return QStringLiteral("注册表");
    case EvidenceSource::Appx: return QStringLiteral("AppX / MSIX");
    case EvidenceSource::Executable: return QStringLiteral("可执行文件");
    case EvidenceSource::Publisher: return QStringLiteral("发布者");
    case EvidenceSource::Folder: return QStringLiteral("目录归属");
    case EvidenceSource::Rule: return QStringLiteral("应用规则");
    case EvidenceSource::RunningProcess: return QStringLiteral("运行进程");
    case EvidenceSource::InstallPath: return QStringLiteral("安装路径");
    }
    return QStringLiteral("未知来源");
}

QVector<const EvidenceInfo *> evidenceFor(const ApplicationInfo &application,
                                          EvidenceSource source)
{
    QVector<const EvidenceInfo *> result;
    for (const EvidenceInfo &evidence : application.evidence) {
        if (evidence.source == source)
            result.append(&evidence);
    }
    return result;
}

bool hasStatus(const QVector<const EvidenceInfo *> &items,
               EvidenceStatus status)
{
    return std::any_of(items.cbegin(), items.cend(), [status](const auto *item) {
        return item->status == status;
    });
}

bool hasCompleteAttributionEvidence(const ApplicationInfo &application,
                                    EvidenceSource source)
{
    const QVector<const EvidenceInfo *> items = evidenceFor(application, source);
    return !items.isEmpty()
            && std::all_of(items.cbegin(), items.cend(), [](const auto *item) {
        return item->status == EvidenceStatus::Matched;
    });
}

void appendUnique(QStringList &values, const QString &value)
{
    if (!value.isEmpty() && !values.contains(value))
        values.append(value);
}

void evaluateNegativeSource(const ApplicationInfo &application,
                            EvidenceSource source,
                            bool required,
                            QStringList &supporting,
                            QStringList &blockers,
                            int &negativeSourceCount)
{
    const QVector<const EvidenceInfo *> items = evidenceFor(application, source);
    if (items.isEmpty()) {
        if (required) {
            appendUnique(blockers,
                         QStringLiteral("缺少%1证据").arg(sourceName(source)));
        }
        return;
    }

    for (const EvidenceInfo *item : items) {
        switch (item->status) {
        case EvidenceStatus::Matched:
            appendUnique(blockers,
                         QStringLiteral("%1仍有正向证据").arg(sourceName(source)));
            break;
        case EvidenceStatus::Partial:
        case EvidenceStatus::Unavailable:
        case EvidenceStatus::Conflict:
        case EvidenceStatus::Incomplete:
        case EvidenceStatus::Ambiguous:
            appendUnique(blockers,
                         QStringLiteral("%1证据不完整或存在冲突")
                                 .arg(sourceName(source)));
            break;
        case EvidenceStatus::NotFound:
            break;
        }
    }

    if (!hasStatus(items, EvidenceStatus::NotFound)) {
        appendUnique(blockers,
                     QStringLiteral("%1没有形成完整否定证据")
                             .arg(sourceName(source)));
        return;
    }

    const bool hasBlockingStatus = std::any_of(
            items.cbegin(), items.cend(), [](const EvidenceInfo *item) {
        return item->status != EvidenceStatus::NotFound;
    });
    if (hasBlockingStatus)
        return;

    ++negativeSourceCount;
    appendUnique(supporting,
                 QStringLiteral("%1未发现安装或活动状态")
                         .arg(sourceName(source)));
}

} // namespace

OrphanAssessment OrphanDetector::assess(
        const ApplicationInfo &application,
        const OrphanDetectionContext &context)
{
    OrphanAssessment result;
    result.evaluated = true;
    result.assessedAt = context.assessedAt.isValid()
            ? context.assessedAt.toUTC() : QDateTime::currentDateTimeUtc();

    if (application.installState == InstallState::Installed) {
        result.state = InstallState::Installed;
        result.summary = QStringLiteral("存在可信安装证据，不属于潜在残留候选。");
        return result;
    }

    if (application.confidence < context.minimumAttributionConfidence) {
        appendUnique(result.blockingReasons,
                     QStringLiteral("应用归属置信度不足"));
    }
    if (!hasCompleteAttributionEvidence(application, EvidenceSource::Rule)
            || !hasCompleteAttributionEvidence(application, EvidenceSource::Folder)) {
        appendUnique(result.blockingReasons,
                     QStringLiteral("缺少精确规则与目录归属证据"));
    }
    if (!context.exclusiveLocations) {
        appendUnique(result.blockingReasons,
                     QStringLiteral("数据目录未声明为应用专属目录"));
    }
    if (!context.scanCompleted || !application.scanComplete) {
        appendUnique(result.blockingReasons,
                     QStringLiteral("目录扫描未完整完成"));
    }

    const qint64 inactivity = application.lastModified.isValid()
            ? application.lastModified.msecsTo(result.assessedAt) : -1;
    if (inactivity < 0) {
        appendUnique(result.blockingReasons,
                     QStringLiteral("缺少可信的最近修改时间"));
    } else if (inactivity < context.minimumInactiveMilliseconds) {
        appendUnique(result.blockingReasons,
                     QStringLiteral("目录近期仍有文件活动"));
    } else {
        const qint64 inactiveDays = inactivity / (24LL * 60LL * 60LL * 1000LL);
        result.supportingEvidence.append(
                QStringLiteral("目录已 %1 天没有文件修改").arg(inactiveDays));
    }

    int negativeSourceCount = 0;
    evaluateNegativeSource(application, EvidenceSource::Executable, true,
                           result.supportingEvidence, result.blockingReasons,
                           negativeSourceCount);
    evaluateNegativeSource(application, EvidenceSource::InstallPath, true,
                           result.supportingEvidence, result.blockingReasons,
                           negativeSourceCount);
    evaluateNegativeSource(application, EvidenceSource::RunningProcess, true,
                           result.supportingEvidence, result.blockingReasons,
                           negativeSourceCount);
    evaluateNegativeSource(application, EvidenceSource::Registry, false,
                           result.supportingEvidence, result.blockingReasons,
                           negativeSourceCount);
    evaluateNegativeSource(application, EvidenceSource::Appx, false,
                           result.supportingEvidence, result.blockingReasons,
                           negativeSourceCount);

    if (!result.blockingReasons.isEmpty()) {
        result.state = InstallState::Unknown;
        result.summary = QStringLiteral(
                "现有证据不足以判断为潜在残留，已保留为状态未知。");
        return result;
    }

    result.state = InstallState::PotentialOrphan;
    int confidence = 80;
    if (application.confidence >= 85)
        confidence += 5;
    if (negativeSourceCount >= 4)
        confidence += 5;
    if (inactivity >= 90LL * 24LL * 60LL * 60LL * 1000LL)
        confidence += 5;
    result.confidence = std::min(confidence, 95);
    result.summary = QStringLiteral(
            "多个完整来源均未发现安装或运行状态，专属 AppData 目录长期未活动，标记为潜在残留。");
    return result;
}

} // namespace wam::core
