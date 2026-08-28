#include "DirectoryScanner.h"
#include "MetadataFingerprint.h"

#include "../../platform/windows/filesystem/ReparsePoint.h"

#include <QDateTime>
#include <QDir>
#include <QSet>

#include <algorithm>
#include <chrono>
#include <system_error>

namespace wam::core {
namespace {

constexpr qsizetype maximumReportedIssues = 100;

QString pathToQString(const std::filesystem::path &path)
{
#ifdef _WIN32
    const auto &nativePath = path.native();
    return QString::fromWCharArray(
            nativePath.data(), static_cast<qsizetype>(nativePath.size()));
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
    const auto systemTime = std::chrono::clock_cast<std::chrono::system_clock>(value);
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

void appendReparseIssue(DirectoryScanStats &stats,
                        const std::filesystem::path &path)
{
    if (stats.issues.size() >= maximumReportedIssues)
        return;
    stats.issues.append({
        ScanErrorCode::PathUnavailable,
        QStringLiteral("已跳过重解析点目录或文件"),
        QStringLiteral("FILE_ATTRIBUTE_REPARSE_POINT"),
        pathToQString(path)
    });
}

void appendChangedDuringScanIssue(DirectoryScanStats &stats,
                                  const std::filesystem::path &path)
{
    if (stats.issues.size() >= maximumReportedIssues)
        return;
    stats.issues.append({
        ScanErrorCode::IoError,
        QStringLiteral("扫描期间目录内容发生变化"),
        QStringLiteral("两次完整元数据快照的路径、大小或修改时间不一致"),
        pathToQString(path)
    });
}

struct ScanPassResult {
    DirectoryScanStats stats;
    MetadataFingerprint fingerprint;
};

QString pathKey(const std::filesystem::path &path)
{
    QString key = QDir::fromNativeSeparators(
            pathToQString(path.lexically_normal()));
#ifdef _WIN32
    key = key.toCaseFolded();
#endif
    return key;
}

ScanPassResult scanPass(
        const std::filesystem::path &rootPath,
        const std::atomic_bool &cancelRequested,
        const DirectoryScanner::FileVisitor &visitor,
        const DirectoryScanner::StatusCallback &statusCallback,
        const QSet<QString> &exclusions,
        bool collectMetadataFingerprint)
{
    ScanPassResult result;
    DirectoryScanStats &stats = result.stats;

    if (cancelRequested.load(std::memory_order_relaxed)) {
        stats.cancelled = true;
        return result;
    }

    if (platform::windows::isReparsePoint(rootPath)) {
        appendReparseIssue(stats, rootPath);
        return result;
    }

    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
            rootPath, std::filesystem::directory_options::none, error);
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        appendIssue(stats, rootPath, error);
        return result;
    }

    while (iterator != end) {
        if (cancelRequested.load(std::memory_order_relaxed)) {
            stats.cancelled = true;
            break;
        }

        const std::filesystem::directory_entry &entry = *iterator;
        const std::filesystem::path path = entry.path();
        const bool excluded = !exclusions.isEmpty()
                && exclusions.contains(pathKey(path));
        if (excluded) {
            error.clear();
            if (entry.is_directory(error) && !error)
                iterator.disable_recursion_pending();
        }

        const bool reparsePoint = !excluded
                && platform::windows::isReparsePoint(path);
        if (reparsePoint) {
            appendReparseIssue(stats, path);
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
                const std::filesystem::path relativePath =
                        path.lexically_relative(rootPath);

                stats.totalSize += size;
                ++stats.fileCount;
                stats.latestModifiedMilliseconds = std::max(
                        stats.latestModifiedMilliseconds, modifiedMilliseconds);
                if (!error && collectMetadataFingerprint) {
                    result.fingerprint.add(
                            pathToQString(relativePath),
                            size, modifiedMilliseconds);
                }
                if (visitor)
                    visitor(relativePath, size, modifiedMilliseconds);

                if (statusCallback
                        && (stats.fileCount == 1
                            || (stats.fileCount & 0xffU) == 0)) {
                    statusCallback(pathToQString(path), stats.fileCount);
                }
            }
        }

        if (error)
            appendIssue(stats, path, error);

        error.clear();
        iterator.increment(error);
        if (error) {
            appendIssue(stats, path, error);
            break;
        }
    }

    if (!stats.cancelled
            && cancelRequested.load(std::memory_order_relaxed)) {
        stats.cancelled = true;
    }
    if (collectMetadataFingerprint)
        stats.metadataFingerprint = result.fingerprint.value();
    return result;
}

} // namespace

DirectoryScanStats DirectoryScanner::scan(const QString &root,
                                          const std::atomic_bool &cancelRequested,
                                          const FileVisitor &visitor,
                                          const StatusCallback &statusCallback,
                                          const QStringList &excludedPaths,
                                          bool verifyStability) const
{
    return scan(root, cancelRequested, visitor, statusCallback,
                excludedPaths, verifyStability, true);
}

DirectoryScanStats DirectoryScanner::scan(const QString &root,
                                          const std::atomic_bool &cancelRequested,
                                          const FileVisitor &visitor,
                                          const StatusCallback &statusCallback,
                                          const QStringList &excludedPaths,
                                          bool verifyStability,
                                          bool collectMetadataFingerprint) const
{
    const std::filesystem::path rootPath = stringToPath(root);

    QSet<QString> exclusions;
    exclusions.reserve(excludedPaths.size());
    for (const QString &path : excludedPaths)
        exclusions.insert(pathKey(stringToPath(path)));

    const bool collectFingerprint = verifyStability
            || collectMetadataFingerprint;

    ScanPassResult first = scanPass(
            rootPath, cancelRequested, visitor, statusCallback,
            exclusions, collectFingerprint);
    if (!verifyStability || first.stats.cancelled || !first.stats.issues.isEmpty())
        return first.stats;

    const ScanPassResult verification = scanPass(
            rootPath, cancelRequested, {}, {}, exclusions, true);
    if (verification.stats.cancelled)
        first.stats.cancelled = true;
    first.stats.issues += verification.stats.issues;
    if (!first.stats.cancelled && verification.stats.issues.isEmpty()) {
        if (first.stats.fileCount != verification.stats.fileCount
                || first.stats.totalSize != verification.stats.totalSize
                || first.stats.metadataFingerprint
                        != verification.stats.metadataFingerprint) {
            appendChangedDuringScanIssue(first.stats, rootPath);
        } else {
            first.stats.stabilityVerified = true;
        }
    }
    return first.stats;
}

} // namespace wam::core
