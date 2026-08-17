#pragma once

#include "../models/ApplicationInfo.h"

#include <QVector>

namespace wam::services {

struct CleanupPlanItem {
    QString id;
    QString applicationId;
    QString applicationName;
    DataCategory category = DataCategory::Unknown;
    QString path;
    QString impact;
    QString ruleSource;
    quint64 size = 0;
    quint64 fileCount = 0;
    RiskLevel risk = RiskLevel::Unknown;
};

struct CleanupPlan {
    QVector<CleanupPlanItem> items;
    quint64 totalSize = 0;
};

// Builds a preview-only plan. It deliberately excludes unknown, sensitive, and non-rebuildable data.
CleanupPlan buildCleanupPlan(const QVector<ApplicationInfo> &applications);

} // namespace wam::services
