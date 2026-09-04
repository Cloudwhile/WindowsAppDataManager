#include "RiskAssessment.h"

namespace wam::core {
namespace {

bool hasAttributionAssessment(const ApplicationInfo &application)
{
    return application.attribution.confidence > 0
            || !application.attribution.evidence.isEmpty()
            || application.attribution.state != AttributionState::Unknown;
}

int attributionConfidence(const ApplicationInfo &application)
{
    return hasAttributionAssessment(application)
            ? application.attribution.confidence : application.confidence;
}

} // namespace

RiskLevel applicationRisk(const ApplicationInfo &application)
{
    if (attributionConfidence(application) < 50)
        return RiskLevel::Unknown;

    bool hasSafe = false;
    bool hasLow = false;
    bool hasCaution = false;
    bool hasUnknown = false;
    bool hasHigh = false;
    for (const DataGroupInfo &group : application.dataGroups) {
        switch (group.risk) {
        case RiskLevel::Protected: return RiskLevel::Protected;
        case RiskLevel::High: hasHigh = true; break;
        case RiskLevel::Unknown: hasUnknown = true; break;
        case RiskLevel::Caution: hasCaution = true; break;
        case RiskLevel::Low: hasLow = true; break;
        case RiskLevel::Safe: hasSafe = true; break;
        }
    }
    if (hasHigh)
        return RiskLevel::High;
    if (hasUnknown)
        return RiskLevel::Unknown;
    if (hasCaution)
        return RiskLevel::Caution;
    if (hasLow)
        return RiskLevel::Low;
    return hasSafe ? RiskLevel::Safe : RiskLevel::Unknown;
}

} // namespace wam::core
