#pragma once

#include "../../models/ScanResult.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <QStringList>

namespace wam::core {

struct DirectoryScanStats {
    quint64 totalSize = 0;
    quint64 fileCount = 0;
    qint64 latestModifiedMilliseconds = 0;
    QVector<ScanIssue> issues;
    bool cancelled = false;
};

class DirectoryScanner final {
public:
    using FileVisitor = std::function<void(const std::filesystem::path &relativePath,
                                           quint64 size,
                                           qint64 modifiedMilliseconds)>;
    using StatusCallback = std::function<void(const QString &currentPath,
                                              quint64 filesVisited)>;

    [[nodiscard]] DirectoryScanStats scan(const QString &root,
                                          const std::atomic_bool &cancelRequested,
                                          const FileVisitor &visitor,
                                          const StatusCallback &statusCallback,
                                          const QStringList &excludedPaths = {}) const;
};

} // namespace wam::core
