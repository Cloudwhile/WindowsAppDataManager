#include "DirectoryScanner.h"

#include "../../platform/windows/filesystem/ReparsePoint.h"

#include <QDateTime>

#include <algorithm>
#include <chrono>
#include <system_error>

namespace wam::core {
namespace {

constexpr qsizetype maximumReportedIssues = 100;

QString pathToQString(const std::filesystem::path &path)
{
#ifdef _WIN32
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

std::filesystem::path stringToPath(const QString &path)
{
#ifdef _WIN32
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}

qint64 toMilliseconds(std::filesystem::file_time_type value)
{
    const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            value - std::filesystem::file_time_type::clock::now()
            + std::chrono::system_clock::now());
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            systemTime.time_since_epoch()).count();
}

ScanErrorCode errorCodeFor(const std::error_code &error)
{
    return error == std::errc::permission_denied
            ? ScanErrorCode::AccessDenied : ScanErrorCode::IoError;
}

void appendIssue(DirectoryScanStats &stats,
                 const std::filesystem::path &path,
                 const std::error_code &error)
{
    if (stats.issues.size() >= maximumReportedIssues)
        return;

    ScanIssue issue;
    issue.code = errorCodeFor(error);
    issue.message = issue.code == ScanErrorCode::AccessDenied
            ? QStringLiteral("无法读取该目录")
            : QStringLiteral("扫描目录时发生 I/O 错误");
    issue.technicalDetail = QString::fromStdString(error.message());
    issue.path = pathToQString(path);
    stats.issues.append(std::move(issue));
}

bool pathsEqual(const std::filesystem::path &left, const std::filesystem::path &right)
{
#ifdef _WIN32
    return pathToQString(left.lexically_normal()).compare(
            pathToQString(right.lexically_normal()), Qt::CaseInsensitive) == 0;
#else
    return left.lexically_normal() == right.lexically_normal();
#endif
}

} // namespace

DirectoryScanStats DirectoryScanner::scan(const QString &root,
                                          const std::atomic_bool &cancelRequested,
                                          const FileVisitor &visitor,
                                          const StatusCallback &statusCallback,
                                          const QStringList &excludedPaths) const
{
    DirectoryScanStats stats;
    const std::filesystem::path rootPath = stringToPath(root);

    if (cancelRequested.load(std::memory_order_relaxed)) {
        stats.cancelled = true;
        return stats;
    }

    QVector<std::filesystem::path> exclusions;
    exclusions.reserve(excludedPaths.size());
    for (const QString &path : excludedPaths)
        exclusions.append(stringToPath(path));

    if (platform::windows::isReparsePoint(rootPath)) {
        ScanIssue issue;
        issue.code = ScanErrorCode::PathUnavailable;
        issue.message = QStringLiteral("已跳过重解析点目录");
        issue.technicalDetail = QStringLiteral("FILE_ATTRIBUTE_REPARSE_POINT");
        issue.path = root;
        stats.issues.append(std::move(issue));
        return stats;
    }

    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
            rootPath, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        appendIssue(stats, rootPath, error);
        return stats;
    }

    while (iterator != end) {
        if (cancelRequested.load(std::memory_order_relaxed)) {
            stats.cancelled = true;
            break;
        }

        const std::filesystem::directory_entry entry = *iterator;
        const std::filesystem::path path = entry.path();

        const bool excluded = std::any_of(exclusions.cbegin(), exclusions.cend(),
                                          [&path](const auto &candidate) {
            return pathsEqual(path, candidate);
        });
        if (excluded) {
            error.clear();
            if (entry.is_directory(error) && !error)
                iterator.disable_recursion_pending();
        }

        const bool reparsePoint = platform::windows::isReparsePoint(path);
        if (reparsePoint) {
            error.clear();
            if (entry.is_directory(error) && !error)
                iterator.disable_recursion_pending();
        }

        error.clear();
        const bool directory = entry.is_directory(error);
        if (!excluded && !reparsePoint && !error && !directory
                && entry.is_regular_file(error)) {
            error.clear();
            const std::uintmax_t rawSize = entry.file_size(error);
            if (!error) {
                error.clear();
                const auto modified = entry.last_write_time(error);
                const qint64 modifiedMilliseconds = error ? 0 : toMilliseconds(modified);
                const quint64 size = static_cast<quint64>(rawSize);

                stats.totalSize += size;
                ++stats.fileCount;
                stats.latestModifiedMilliseconds = std::max(
                        stats.latestModifiedMilliseconds, modifiedMilliseconds);
                if (visitor)
                    visitor(path.lexically_relative(rootPath), size, modifiedMilliseconds);

                if (statusCallback && (stats.fileCount & 0x3ffU) == 0)
                    statusCallback(pathToQString(path), stats.fileCount);
            }
        }

        if (error)
            appendIssue(stats, path, error);

        error.clear();
        iterator.increment(error);
        if (error) {
            appendIssue(stats, path, error);
            error.clear();
        }
    }

    return stats;
}

} // namespace wam::core
