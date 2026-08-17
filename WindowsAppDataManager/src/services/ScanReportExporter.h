#pragma once

#include "../models/ApplicationInfo.h"
#include "../models/ScanResult.h"

#include <QUrl>
#include <QVector>
#include <QString>

namespace wam::services {

// Exports the latest scan as UTF-8 CSV or structured JSON by file suffix.
QString exportScanReport(const QUrl &destination,
                         const QVector<ApplicationInfo> &applications,
                         const QVector<ScanIssue> &issues);

} // namespace wam::services
