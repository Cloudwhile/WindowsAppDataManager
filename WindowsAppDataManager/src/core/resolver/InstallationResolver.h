#pragma once

#include "../../models/ApplicationInfo.h"

namespace wam::core {

struct InstallationEvidenceScore {
    int strongEvidenceCount = 0;
    int negativeEvidenceCount = 0;
    int requiredNegativeEvidenceCount = 0;
    bool evidenceComplete = false;
    bool conflict = false;
};

class InstallationResolver final {
public:
    [[nodiscard]] static InstallationAssessment evaluate(
            const InstallationEvidenceScore &evidence);
};

} // namespace wam::core
