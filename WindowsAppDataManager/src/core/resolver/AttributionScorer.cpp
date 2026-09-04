#include "AttributionScorer.h"

#include <algorithm>

namespace wam::core {

AttributionScoreResult AttributionScorer::evaluate(
        const AttributionEvidenceScore &evidence)
{
    AttributionScoreResult result;
    if (evidence.exactRule) {
        result.state = AttributionState::Verified;
        result.confidence = 100;
    }

    const int strongEvidenceCount = static_cast<int>(evidence.executableMatched)
            + static_cast<int>(evidence.registryMatched)
            + static_cast<int>(evidence.appxMatched);
    if (strongEvidenceCount >= 2)
        result.compatibilityConfidence = 98;
    else if (evidence.executableMatched && evidence.executableSignatureMatched)
        result.compatibilityConfidence = 97;
    else if (evidence.appxMatched)
        result.compatibilityConfidence = 96;
    else if (evidence.registryMatched)
        result.compatibilityConfidence = 94;
    else if (evidence.executableMatched)
        result.compatibilityConfidence = 88;
    else if (evidence.registryPositive || evidence.appxPositive)
        result.compatibilityConfidence = 90;
    else
        result.compatibilityConfidence = 72;

    if (evidence.ambiguous)
        result.compatibilityConfidence = std::min(result.compatibilityConfidence, 96);
    if (evidence.executableSignatureUntrusted)
        result.compatibilityConfidence = std::min(result.compatibilityConfidence, 90);
    if (evidence.conflict) {
        result.compatibilityConfidence = strongEvidenceCount == 0
                ? 49 : std::min(result.compatibilityConfidence, 90);
    }
    return result;
}

AttributionScoreResult AttributionScorer::evaluateInference(
        const InferenceScore &evidence)
{
    AttributionScoreResult result;
    const int weightedScore = std::clamp(
            evidence.folder + evidence.registry + evidence.appx
                    + evidence.installPath + evidence.publisher
                    + evidence.executable + evidence.process,
            0, 100);
    if (evidence.ambiguous) {
        result.state = AttributionState::Unknown;
    } else if (evidence.margin < 15
            || evidence.independentEvidenceCount < 2) {
        result.state = weightedScore >= 45
                ? AttributionState::Suggested : AttributionState::Unknown;
    } else if (weightedScore >= 75) {
        result.state = AttributionState::StrongInferred;
    } else if (weightedScore >= 45) {
        result.state = AttributionState::Suggested;
    }
    result.confidence = weightedScore;
    result.compatibilityConfidence = weightedScore;
    if (evidence.ambiguous)
        result.confidence = std::min(result.confidence, 59);
    return result;
}

} // namespace wam::core
