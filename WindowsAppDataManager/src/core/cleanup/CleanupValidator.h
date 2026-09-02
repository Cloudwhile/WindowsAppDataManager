#pragma once

#include "../../models/CleanupPlan.h"
#include "../../platform/windows/process/RunningProcessCatalog.h"

#include <atomic>

namespace wam::core {

enum class CleanupValidationState {
    Ready,
    Missing,
    Cancelled,
    Blocked
};

struct CleanupValidationResult {
    CleanupValidationState state = CleanupValidationState::Blocked;
    QString message;
    QString technicalDetail;
};

class CleanupValidator final {
public:
    [[nodiscard]] static CleanupValidationResult validateProcessState(
            const CleanupCandidateInfo &candidate,
            const platform::windows::RunningProcessQueryResult &processes);

    [[nodiscard]] static CleanupValidationResult validate(
            const CleanupCandidateInfo &candidate,
            const QStringList &scanRoots,
            const platform::windows::RunningProcessQueryResult &processes);
    [[nodiscard]] static CleanupValidationResult validate(
            const CleanupCandidateInfo &candidate,
            const QStringList &scanRoots,
            const platform::windows::RunningProcessQueryResult &processes,
            const std::atomic_bool &cancelRequested);
};

} // namespace wam::core
