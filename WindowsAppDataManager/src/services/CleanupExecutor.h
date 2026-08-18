#pragma once

#include "../models/ApplicationInfo.h"

#include <QString>

#include <memory>

namespace wam::services {

struct CleanupExecutionOutcome {
    bool succeeded = false;
    bool recoverable = false;
    bool aborted = false;
    quint32 nativeError = 0;
    QString message;
    QString technicalDetail;
};

class CleanupExecutor {
public:
    virtual ~CleanupExecutor() = default;

    [[nodiscard]] virtual CleanupExecutionOutcome moveToRecycleBin(
            const CleanupCandidateInfo &candidate) = 0;
};

using CleanupExecutorPtr = std::shared_ptr<CleanupExecutor>;

} // namespace wam::services
