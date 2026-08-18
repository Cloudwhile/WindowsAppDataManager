#pragma once

#include "../../models/CleanupPlan.h"

namespace wam::core {

struct CleanupPlanBuildContext {
    QStringList scanRoots;
    int minimumApplicationConfidence = 85;
    QDateTime createdAt;
};

class CleanupPlanBuilder final {
public:
    [[nodiscard]] static CleanupPlan build(
            const QVector<ApplicationInfo> &applications,
            const CleanupPlanBuildContext &context);
};

} // namespace wam::core
