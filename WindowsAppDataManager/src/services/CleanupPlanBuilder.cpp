#include "CleanupPlanBuilder.h"

#include <algorithm>

namespace wam::services {
namespace {

bool isEligible(const ApplicationInfo &application, const DataGroupInfo &group)
{
    const bool safeRisk = group.risk == RiskLevel::Safe || group.risk == RiskLevel::Low;
    return application.installState == InstallState::Installed
            && application.confidence >= 70
            && !group.ruleSource.isEmpty()
            && group.size > 0
            && group.rebuildable == RebuildableState::Yes
            && safeRisk;
}

} // namespace

CleanupPlan buildCleanupPlan(const QVector<ApplicationInfo> &applications)
{
    CleanupPlan plan;
    for (const ApplicationInfo &application : applications) {
        for (const DataGroupInfo &group : application.dataGroups) {
            if (!isEligible(application, group))
                continue;

            plan.items.append({
                application.id + QLatin1Char(':') + group.id,
                application.id,
                application.name,
                group.category,
                group.path,
                group.impact,
                group.ruleSource,
                group.size,
                group.fileCount,
                group.risk
            });
            plan.totalSize += group.size;
        }
    }

    std::sort(plan.items.begin(), plan.items.end(), [](const auto &left, const auto &right) {
        return left.size > right.size;
    });
    return plan;
}

} // namespace wam::services
