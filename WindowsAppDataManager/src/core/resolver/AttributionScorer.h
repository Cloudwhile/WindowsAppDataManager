#pragma once

#include "../../models/ApplicationInfo.h"

namespace wam::core {

struct AttributionEvidenceScore {
    bool exactRule = false;
    bool executableMatched = false;
    bool executableSignatureMatched = false;
    bool registryMatched = false;
    bool registryPositive = false;
    bool appxMatched = false;
    bool appxPositive = false;
    bool ambiguous = false;
    bool executableSignatureUntrusted = false;
    bool conflict = false;
};

struct AttributionScoreResult {
    AttributionState state = AttributionState::Unknown;
    int confidence = 0;
    int compatibilityConfidence = 0;
};

struct InferenceScore {
    int folder = 0;
    int registry = 0;
    int appx = 0;
    int installPath = 0;
    int publisher = 0;
    int executable = 0;
    int process = 0;
    int independentEvidenceCount = 0;
    int margin = 0;
    bool ambiguous = false;
};

class AttributionScorer final {
public:
    [[nodiscard]] static AttributionScoreResult evaluate(
            const AttributionEvidenceScore &evidence);
    [[nodiscard]] static AttributionScoreResult evaluateInference(
            const InferenceScore &evidence);
};

} // namespace wam::core
