#include "InstallationResolver.h"

#include <algorithm>

namespace wam::core {

InstallationAssessment InstallationResolver::evaluate(
        const InstallationEvidenceScore &evidence)
{
    InstallationAssessment result;
    if (evidence.strongEvidenceCount > 0) {
        result.state = InstallationState::Installed;
        result.confidence = std::min(100, 60 + evidence.strongEvidenceCount * 15);
        return result;
    }

    // 冲突、部分枚举或不可用来源都不能被误解为“未安装”。只有所有必需
    // 的否定来源均完整返回 NotFound，才允许给出 NotObserved。
    if (evidence.conflict || !evidence.evidenceComplete
            || evidence.requiredNegativeEvidenceCount <= 0
            || evidence.negativeEvidenceCount
                    < evidence.requiredNegativeEvidenceCount) {
        return result;
    }

    result.state = InstallationState::NotObserved;
    result.confidence = std::min(
            95, 60 + evidence.negativeEvidenceCount * 10);
    return result;
}

} // namespace wam::core
