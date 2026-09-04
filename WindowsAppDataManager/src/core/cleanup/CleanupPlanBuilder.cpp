#include "CleanupPlanBuilder.h"

#include "../rules/RulePathResolver.h"

#include <QHash>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <iterator>
#include <set>

namespace wam::core {
namespace {

bool strictDescendantOf(const QString &path, const QString &root)
{
    const QString pathKey = rules::normalizedPathKey(path);
    const QString rootKey = rules::normalizedPathKey(root);
    return !pathKey.isEmpty() && !rootKey.isEmpty()
            && pathKey.startsWith(rootKey + QLatin1Char('/'));
}

bool insideAnyRoot(const QString &path, const QStringList &roots)
{
    return std::any_of(roots.cbegin(), roots.cend(), [&path](const QString &root) {
        return strictDescendantOf(path, root);
    });
}

bool completeInactiveProcessEvidence(const ApplicationInfo &application)
{
    const QVector<EvidenceInfo> &evidenceItems =
            application.installation.evidence.isEmpty()
            ? application.evidence : application.installation.evidence;
    bool observed = false;
    for (const EvidenceInfo &evidence : evidenceItems) {
        if (evidence.source != EvidenceSource::RunningProcess)
            continue;
        observed = true;
        if (evidence.status != EvidenceStatus::NotFound)
            return false;
    }
    return observed;
}

bool hasAttributionAssessment(const ApplicationInfo &application)
{
    return application.attribution.confidence > 0
            || !application.attribution.evidence.isEmpty()
            || application.attribution.state != AttributionState::Unknown;
}

bool hasInstallationAssessment(const ApplicationInfo &application)
{
    return application.installation.confidence > 0
            || !application.installation.evidence.isEmpty()
            || application.installation.state != InstallationState::Unknown;
}

int attributionConfidence(const ApplicationInfo &application)
{
    return hasAttributionAssessment(application)
            ? application.attribution.confidence : application.confidence;
}

bool installationConfirmed(const ApplicationInfo &application)
{
    return hasInstallationAssessment(application)
            ? application.installation.state == InstallationState::Installed
            : application.installState == InstallState::Installed;
}

QString rejectionReason(const ApplicationInfo &application,
                        const CleanupCandidateInfo &candidate,
                        const CleanupPlanBuildContext &context)
{
    if (candidate.id.isEmpty() || candidate.ruleEntryId.isEmpty()
            || candidate.applicationId != application.id) {
        return QStringLiteral("清理候选标识或应用归属无效");
    }
    if (!installationConfirmed(application))
        return QStringLiteral("应用未确认仍处于安装状态");
    if (hasAttributionAssessment(application)
            && application.attribution.state != AttributionState::Verified) {
        return QStringLiteral("应用归属未达到已验证级别");
    }
    if (attributionConfidence(application) < context.minimumApplicationConfidence)
        return QStringLiteral("应用归属置信度不足");
    if (!application.scanComplete || !candidate.scanComplete)
        return QStringLiteral("目录扫描未完整完成");
    if (!candidate.verifiedRule
            || !candidate.ruleSource.startsWith(QStringLiteral("内置规则 / "))) {
        return QStringLiteral("清理目标不是已验证内置规则");
    }
    if (candidate.ruleOrigin != RuleOrigin::BuiltIn
            || candidate.ruleTrustLevel != RuleTrustLevel::Verified) {
        return QStringLiteral("清理目标不是已验证内置规则");
    }
    if (!candidate.exclusiveLocation)
        return QStringLiteral("清理目标不属于应用专属目录");
    if (candidate.risk != RiskLevel::Safe
            || candidate.rebuildable != RebuildableState::Yes) {
        return QStringLiteral("数据风险或可重建性不满足自动计划要求");
    }
    if (candidate.containsUnsafeData)
        return QStringLiteral("候选目录包含敏感或未验证数据");
    if (candidate.fileCount == 0 || candidate.metadataFingerprint.isEmpty())
        return QStringLiteral("候选目录缺少完整元数据快照");
    if (!candidate.identityValid || !candidate.directory)
        return QStringLiteral("候选目录缺少稳定文件系统身份");
    if (candidate.executablePath.isEmpty())
        return QStringLiteral("应用缺少可验证的可执行文件路径");
    if (!strictDescendantOf(candidate.path, candidate.applicationRoot))
        return QStringLiteral("候选路径不在应用数据目录内部");
    if (!insideAnyRoot(candidate.path, context.scanRoots))
        return QStringLiteral("候选路径不在本次扫描范围内");
    if (!completeInactiveProcessEvidence(application))
        return QStringLiteral("运行进程状态未完整确认");
    return {};
}

void addRejection(QHash<QString, int> &rejections, const QString &reason)
{
    rejections[reason] = rejections.value(reason) + 1;
}

struct PathComponentsLess {
    bool operator()(const QStringList &left, const QStringList &right) const
    {
        return std::lexicographical_compare(
                left.cbegin(), left.cend(), right.cbegin(), right.cend(),
                [](const QString &leftPart, const QString &rightPart) {
            return leftPart.compare(rightPart, Qt::CaseSensitive) < 0;
        });
    }
};

bool componentsOverlap(const QStringList &left, const QStringList &right)
{
    const qsizetype sharedSize = std::min(left.size(), right.size());
    return std::equal(left.cbegin(), left.cbegin() + sharedSize,
                      right.cbegin());
}

QStringList pathComponents(const QString &normalizedPath)
{
    return normalizedPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

bool overlapsAcceptedPath(
        const std::set<QStringList, PathComponentsLess> &acceptedPaths,
        const QStringList &candidatePath)
{
    const auto next = acceptedPaths.lower_bound(candidatePath);
    if (next != acceptedPaths.cend()
            && componentsOverlap(candidatePath, *next)) {
        return true;
    }
    if (next == acceptedPaths.cbegin())
        return false;

    return componentsOverlap(candidatePath, *std::prev(next));
}

} // namespace

CleanupPlan CleanupPlanBuilder::build(
        const QVector<ApplicationInfo> &applications,
        const CleanupPlanBuildContext &context)
{
    CleanupPlan plan;
    plan.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    plan.createdAt = context.createdAt.isValid()
            ? context.createdAt.toUTC() : QDateTime::currentDateTimeUtc();

    QHash<QString, int> rejections;
    QSet<QString> acceptedIds;
    std::set<QStringList, PathComponentsLess> acceptedPaths;
    for (const ApplicationInfo &application : applications) {
        for (const CleanupCandidateInfo &candidate : application.cleanupCandidates) {
            QString reason = rejectionReason(application, candidate, context);
            const QString candidatePathKey = rules::normalizedPathKey(candidate.path);
            const QStringList candidatePath = pathComponents(candidatePathKey);
            if (reason.isEmpty() && acceptedIds.contains(candidate.id)) {
                reason = QStringLiteral("清理候选标识重复");
            }
            if (reason.isEmpty()
                    && overlapsAcceptedPath(acceptedPaths, candidatePath)) {
                reason = QStringLiteral("清理目标与其他候选路径重叠");
            }
            if (!reason.isEmpty()) {
                ++plan.excludedCount;
                addRejection(rejections, reason);
                continue;
            }

            CleanupPlanItem item;
            item.candidate = candidate;
            plan.estimatedSize += candidate.size;
            acceptedIds.insert(candidate.id);
            acceptedPaths.insert(candidatePath);
            plan.items.append(std::move(item));
        }
    }

    std::sort(plan.items.begin(), plan.items.end(), [](const auto &left, const auto &right) {
        if (left.candidate.size != right.candidate.size)
            return left.candidate.size > right.candidate.size;
        return left.candidate.path.compare(
                right.candidate.path, Qt::CaseInsensitive) < 0;
    });

    QStringList reasons = rejections.keys();
    reasons.sort(Qt::CaseInsensitive);
    for (const QString &reason : reasons) {
        plan.exclusionReasons.append(
                QStringLiteral("%1（%2 项）").arg(reason).arg(rejections.value(reason)));
    }
    return plan;
}

} // namespace wam::core
