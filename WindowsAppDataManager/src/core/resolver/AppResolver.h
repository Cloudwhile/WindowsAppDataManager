#pragma once

#include "../../models/ApplicationInfo.h"

#include <QStringList>

namespace wam::core {

struct ScanTarget {
    ApplicationInfo application;
    QString path;
    QStringList excludedPaths;
};

class AppResolver final {
public:
    [[nodiscard]] QVector<ScanTarget> discoverTargets(const QStringList &roots) const;
};

} // namespace wam::core
