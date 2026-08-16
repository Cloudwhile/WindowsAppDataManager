#pragma once

#include "../rules/RuleCatalog.h"
#include "../../models/ApplicationInfo.h"
#include "../../models/InstallationEvidence.h"

#include <QStringList>

namespace wam::core {

struct ScanTarget {
    ApplicationInfo application;
    QString path;
    QStringList excludedPaths;
    QVector<RuleEntry> classificationRules;
    QString ruleSource;
};

class AppResolver final {
public:
    AppResolver();
    explicit AppResolver(InstallationEvidenceSnapshot evidence);
    explicit AppResolver(rules::RuleCatalog catalog);
    AppResolver(rules::RuleCatalog catalog, InstallationEvidenceSnapshot evidence);

    [[nodiscard]] QVector<ScanTarget> discoverTargets(const QStringList &roots) const;

private:
    rules::RuleCatalog m_catalog;
    InstallationEvidenceSnapshot m_evidence;
};

} // namespace wam::core
