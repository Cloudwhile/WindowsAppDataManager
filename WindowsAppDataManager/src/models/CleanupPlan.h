#pragma once

#include "ApplicationInfo.h"

#include <QDateTime>
#include <QStringList>
#include <QVector>

namespace wam {

enum class CleanupItemState {
    Pending,
    Validating,
    Ready,
    Cleaning,
    Done,
    Skipped,
    Failed
};

struct CleanupPlanItem {
    CleanupCandidateInfo candidate;
    CleanupItemState state = CleanupItemState::Pending;
    QString statusMessage;
    quint64 releasedSize = 0;
    bool selected = false;
};

struct CleanupPlan {
    QString id;
    QDateTime createdAt;
    QVector<CleanupPlanItem> items;
    QStringList exclusionReasons;
    quint64 estimatedSize = 0;
    int excludedCount = 0;
};

struct CleanupHistoryRecord {
    QString runId;
    QDateTime createdAt;
    QDateTime completedAt;
    quint64 estimatedSize = 0;
    quint64 releasedSize = 0;
    int itemCount = 0;
    int successCount = 0;
    int failureCount = 0;
    bool recoverable = false;
};

struct CleanupRunResult {
    CleanupPlan plan;
    CleanupHistoryRecord history;
    QString errorMessage;
    QString technicalDetail;
    bool cancelled = false;
    bool filesystemOperationAttempted = false;
};

} // namespace wam

Q_DECLARE_METATYPE(wam::CleanupItemState)
Q_DECLARE_METATYPE(wam::CleanupPlanItem)
Q_DECLARE_METATYPE(wam::CleanupPlan)
Q_DECLARE_METATYPE(wam::CleanupHistoryRecord)
Q_DECLARE_METATYPE(wam::CleanupRunResult)
