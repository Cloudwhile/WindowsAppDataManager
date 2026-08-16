#pragma once

#include "../../models/RuleDefinition.h"

#include <QByteArray>

#include <optional>

namespace wam::core::rules {

struct RuleLoadResult {
    std::optional<ApplicationRule> rule;
    QVector<RuleLoadIssue> issues;

    [[nodiscard]] bool isValid() const { return rule.has_value() && issues.isEmpty(); }
};

class RuleLoader final {
public:
    [[nodiscard]] static RuleLoadResult load(const QByteArray &json,
                                             const QString &sourceName);
};

} // namespace wam::core::rules
