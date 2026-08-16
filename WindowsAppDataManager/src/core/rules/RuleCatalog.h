#pragma once

#include "RuleLoader.h"

namespace wam::core::rules {

struct RuleDocument {
    QString sourceName;
    QByteArray json;
};

class RuleCatalog final {
public:
    RuleCatalog() = default;

    [[nodiscard]] static RuleCatalog fromJsonDocuments(
            const QVector<RuleDocument> &documents);
    [[nodiscard]] static const RuleCatalog &builtIn();

    [[nodiscard]] const QVector<ApplicationRule> &applications() const;
    [[nodiscard]] const QVector<RuleLoadIssue> &issues() const;
    [[nodiscard]] const ApplicationRule *findById(const QString &applicationId) const;

private:
    QVector<ApplicationRule> m_applications;
    QVector<RuleLoadIssue> m_issues;
};

} // namespace wam::core::rules
