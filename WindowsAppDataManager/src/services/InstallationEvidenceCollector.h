#pragma once

#include "../core/rules/RuleCatalog.h"
#include "../models/InstallationEvidence.h"

namespace wam::services {

class InstallationEvidenceCollector final {
public:
    [[nodiscard]] static InstallationEvidenceSnapshot collect(
            const core::rules::RuleCatalog &catalog);
};

} // namespace wam::services
