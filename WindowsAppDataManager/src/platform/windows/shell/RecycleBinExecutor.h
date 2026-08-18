#pragma once

#include "../../../services/CleanupExecutor.h"

namespace wam::platform::windows {

class RecycleBinExecutor final : public services::CleanupExecutor {
public:
    [[nodiscard]] services::CleanupExecutionOutcome moveToRecycleBin(
            const CleanupCandidateInfo &candidate) override;
};

} // namespace wam::platform::windows
