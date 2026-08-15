#pragma once

#include "ApplicationInfo.h"

#include <QMetaType>
#include <QStringList>

namespace wam {

enum class ScanErrorCode {
    AccessDenied,
    PathUnavailable,
    IoError,
    Cancelled
};

struct ScanIssue {
    ScanErrorCode code = ScanErrorCode::IoError;
    QString message;
    QString technicalDetail;
    QString path;
};

struct ScanResult {
    QVector<ApplicationInfo> applications;
    QVector<ScanIssue> issues;
    QStringList roots;
    quint64 totalSize = 0;
    quint64 fileCount = 0;
    qint64 elapsedMilliseconds = 0;
    bool cancelled = false;
};

} // namespace wam

Q_DECLARE_METATYPE(wam::ScanResult)
