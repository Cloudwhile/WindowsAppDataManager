#pragma once

#include <QString>

namespace wam::core::rules {

enum class RulePathResolutionStatus {
    Resolved,
    MissingEnvironmentVariable,
    Invalid
};

struct RulePathResolution {
    RulePathResolutionStatus status = RulePathResolutionStatus::Invalid;
    QString path;
    QString detail;

    [[nodiscard]] bool isResolved() const
    {
        return status == RulePathResolutionStatus::Resolved;
    }
};

[[nodiscard]] bool validateRulePath(const QString &value, QString *errorMessage = nullptr);
[[nodiscard]] RulePathResolution resolveRulePath(QString value);
[[nodiscard]] QString normalizedRulePathClaim(const QString &value);
[[nodiscard]] QString expandRulePath(QString value);
[[nodiscard]] QString normalizedPathKey(const QString &path);

} // namespace wam::core::rules
