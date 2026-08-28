#pragma once

#include "../../models/ApplicationInfo.h"
#include "../../models/RuleDefinition.h"

#include <QStringList>

#include <filesystem>

namespace wam::core {

struct Classification {
    QString id;
    DataCategory category = DataCategory::Unknown;
    RiskLevel risk = RiskLevel::Unknown;
    RebuildableState rebuildable = RebuildableState::Unknown;
    QString impact;
    QString ruleSource;
    QString matchedPath;
    bool verifiedRule = false;
};

class DataClassifier final {
public:
    DataClassifier() = default;
    DataClassifier(const QVector<RuleEntry> &applicationRules,
                   QString ruleSource);

    [[nodiscard]] Classification classify(const std::filesystem::path &relativePath) const;
    [[nodiscard]] Classification classify(const std::filesystem::path &relativePath,
                                          const QVector<RuleEntry> &applicationRules,
                                          const QString &ruleSource) const;

private:
    [[nodiscard]] Classification classifyNormalizedPath(
            const QString &relativePath) const;

    QVector<RuleEntry> m_applicationRules;
    QStringList m_normalizedRulePaths;
    QString m_ruleSource;
};

} // namespace wam::core
