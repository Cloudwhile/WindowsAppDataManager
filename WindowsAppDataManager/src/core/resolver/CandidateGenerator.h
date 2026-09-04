#pragma once

#include "../../models/ApplicationInfo.h"
#include "../../models/InstallationEvidence.h"

#include <QString>
#include <QVector>

namespace wam::core {

struct InferredApplicationCandidate {
    QString name;
    QString publisher;
    QString installPath;
    AttributionAssessment attribution;
    InstallationAssessment installation;
    int score = 0;
    bool ambiguous = false;
};

class CandidateGenerator final {
public:
    [[nodiscard]] static QVector<InferredApplicationCandidate> generate(
            const QString &directoryPath,
            const InstallationEvidenceSnapshot &evidence);
};

} // namespace wam::core
