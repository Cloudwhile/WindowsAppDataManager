#pragma once

#include "../../models/ApplicationInfo.h"

namespace wam::core {

struct OrphanDetectionContext {
    bool exclusiveLocations = false;
    bool scanCompleted = false;
    QDateTime assessedAt;
    int minimumAttributionConfidence = 70;
    qint64 minimumInactiveMilliseconds = 30LL * 24LL * 60LL * 60LL * 1000LL;
};

class OrphanDetector final {
public:
    [[nodiscard]] static OrphanAssessment assess(
            const ApplicationInfo &application,
            const OrphanDetectionContext &context);
};

} // namespace wam::core
