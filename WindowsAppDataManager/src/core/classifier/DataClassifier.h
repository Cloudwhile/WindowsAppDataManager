#pragma once

#include "../../models/ApplicationInfo.h"

#include <filesystem>

namespace wam::core {

struct Classification {
    QString id;
    DataCategory category = DataCategory::Unknown;
    RiskLevel risk = RiskLevel::Unknown;
    RebuildableState rebuildable = RebuildableState::Unknown;
    QString impact;
    QString ruleSource;
};

class DataClassifier final {
public:
    [[nodiscard]] Classification classify(const std::filesystem::path &relativePath) const;
};

} // namespace wam::core
