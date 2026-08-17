#include "ExecutableFileIdentity.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <array>
#include <exception>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace wam::platform::windows {
namespace {

#ifdef Q_OS_WIN
class UniqueHandle final {
public:
    explicit UniqueHandle(HANDLE handle = INVALID_HANDLE_VALUE) noexcept
        : m_handle(handle)
    {
    }

    ~UniqueHandle()
    {
        if (m_handle != INVALID_HANDLE_VALUE)
            CloseHandle(m_handle);
    }

    UniqueHandle(const UniqueHandle &) = delete;
    UniqueHandle &operator=(const UniqueHandle &) = delete;

    UniqueHandle(UniqueHandle &&other) noexcept
        : m_handle(other.release())
    {
    }

    UniqueHandle &operator=(UniqueHandle &&other) noexcept
    {
        if (this == &other)
            return *this;
        if (m_handle != INVALID_HANDLE_VALUE)
            CloseHandle(m_handle);
        m_handle = other.release();
        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept
    {
        return m_handle;
    }

    [[nodiscard]] HANDLE release() noexcept
    {
        return std::exchange(m_handle, INVALID_HANDLE_VALUE);
    }

private:
    HANDLE m_handle = INVALID_HANDLE_VALUE;
};

QString extendedNativePath(const QString &path)
{
    QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    if (native.startsWith(QStringLiteral("\\\\?\\")) || native.size() < 248)
        return native;
    if (native.startsWith(QStringLiteral("\\\\")))
        return QStringLiteral("\\\\?\\UNC\\") + native.sliced(2);
    return QStringLiteral("\\\\?\\") + native;
}

QString nativeErrorText(DWORD status)
{
    std::array<wchar_t, 512> buffer {};
    const DWORD length = FormatMessageW(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            status,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            nullptr);
    if (length == 0)
        return QStringLiteral("Win32 error %1").arg(status);
    return QString::fromWCharArray(
            buffer.data(), static_cast<qsizetype>(length)).trimmed();
}

quint64 combinedValue(DWORD high, DWORD low) noexcept
{
    return (static_cast<quint64>(high) << 32U) | static_cast<quint64>(low);
}

quint64 fileTimeValue(const FILETIME &value) noexcept
{
    return combinedValue(value.dwHighDateTime, value.dwLowDateTime);
}

bool missingPathError(DWORD status) noexcept
{
    return status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND
            || status == ERROR_INVALID_NAME;
}

bool isAsciiDriveLetter(QChar value) noexcept
{
    const ushort code = value.unicode();
    return (code >= static_cast<ushort>('A') && code <= static_cast<ushort>('Z'))
            || (code >= static_cast<ushort>('a') && code <= static_cast<ushort>('z'));
}

QString dosPath(const QString &path)
{
    QString normalized = QDir::fromNativeSeparators(path);
    if (normalized.startsWith(QStringLiteral("//?/")))
        normalized.remove(0, 4);
    return normalized;
}

bool localDriveRoot(const QString &path, QString &root, DWORD &status)
{
    const QString normalized = dosPath(path);
    if (normalized.size() < 3 || !isAsciiDriveLetter(normalized.at(0))
            || normalized.at(1) != QLatin1Char(':')
            || normalized.at(2) != QLatin1Char('/')) {
        status = normalized.startsWith(QStringLiteral("//"))
                ? ERROR_BAD_NETPATH : ERROR_INVALID_NAME;
        return false;
    }

    root = normalized.left(1) + QStringLiteral(":\\");
    const UINT driveType = GetDriveTypeW(
            reinterpret_cast<const wchar_t *>(root.utf16()));
    if (driveType == DRIVE_REMOTE) {
        status = ERROR_BAD_NETPATH;
        return false;
    }
    if (driveType == DRIVE_UNKNOWN || driveType == DRIVE_NO_ROOT_DIR) {
        status = ERROR_INVALID_DRIVE;
        return false;
    }
    status = ERROR_SUCCESS;
    return true;
}

bool pathSegments(const QString &path,
                  QStringList &segments,
                  DWORD &status)
{
    const QString normalized = dosPath(path);
    const QStringList rawSegments = normalized.sliced(3).split(
            QLatin1Char('/'), Qt::KeepEmptyParts);
    if (rawSegments.isEmpty()) {
        status = ERROR_DIRECTORY;
        return false;
    }

    for (const QString &segment : rawSegments) {
        if (segment.isEmpty() || segment == QStringLiteral(".")
                || segment == QStringLiteral("..")) {
            status = ERROR_INVALID_NAME;
            return false;
        }
        segments.append(segment);
    }
    status = ERROR_SUCCESS;
    return true;
}

bool reparsePointAttributes(HANDLE file, DWORD &attributes, DWORD &status) noexcept
{
    BY_HANDLE_FILE_INFORMATION information {};
    if (!GetFileInformationByHandle(file, &information)) {
        status = GetLastError();
        return false;
    }
    attributes = information.dwFileAttributes;
    status = ERROR_SUCCESS;
    return true;
}

ExecutableFileIdentity identityFromHandle(HANDLE file, DWORD &status) noexcept
{
    BY_HANDLE_FILE_INFORMATION information {};
    if (!GetFileInformationByHandle(file, &information)) {
        status = GetLastError();
        return {};
    }
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        status = ERROR_DIRECTORY;
        return {};
    }

    ExecutableFileIdentity identity;
    identity.valid = true;
    identity.volumeSerialNumber = information.dwVolumeSerialNumber;
    identity.fileIndex = combinedValue(
            information.nFileIndexHigh, information.nFileIndexLow);
    identity.fileSize = combinedValue(
            information.nFileSizeHigh, information.nFileSizeLow);
    identity.lastWriteTime = fileTimeValue(information.ftLastWriteTime);
    status = ERROR_SUCCESS;
    return identity;
}

QString normalizedFinalPath(HANDLE file, DWORD &status)
{
    constexpr DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
    SetLastError(ERROR_SUCCESS);
    const DWORD required = GetFinalPathNameByHandleW(file, nullptr, 0, flags);
    if (required == 0) {
        status = GetLastError();
        return {};
    }

    std::vector<wchar_t> buffer(static_cast<size_t>(required) + 1U);
    const DWORD written = GetFinalPathNameByHandleW(
            file, buffer.data(), static_cast<DWORD>(buffer.size()), flags);
    if (written == 0 || written >= static_cast<DWORD>(buffer.size())) {
        status = written == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
        return {};
    }

    status = ERROR_SUCCESS;
    return QString::fromWCharArray(
            buffer.data(), static_cast<qsizetype>(written));
}

bool isLocalDosFinalPath(const QString &path, DWORD &status)
{
    constexpr qsizetype extendedDrivePrefixLength = 7;
    if (path.size() < extendedDrivePrefixLength
            || !path.startsWith(QStringLiteral("\\\\?\\"))
            || path.at(5) != QLatin1Char(':')
            || (path.at(6) != QLatin1Char('\\')
                && path.at(6) != QLatin1Char('/'))) {
        status = path.startsWith(QStringLiteral("\\\\?\\UNC\\"),
                                 Qt::CaseInsensitive)
                ? ERROR_BAD_NETPATH : ERROR_INVALID_NAME;
        return false;
    }

    const ushort drive = path.at(4).unicode();
    if (!((drive >= static_cast<ushort>('A')
           && drive <= static_cast<ushort>('Z'))
          || (drive >= static_cast<ushort>('a')
              && drive <= static_cast<ushort>('z')))) {
        status = ERROR_INVALID_DRIVE;
        return false;
    }

    QString driveRoot;
    driveRoot.reserve(3);
    driveRoot.append(path.at(4));
    driveRoot.append(QStringLiteral(":\\"));
    const UINT driveType = GetDriveTypeW(
            reinterpret_cast<const wchar_t *>(driveRoot.utf16()));
    if (driveType == DRIVE_REMOTE) {
        status = ERROR_BAD_NETPATH;
        return false;
    }
    if (driveType == DRIVE_UNKNOWN || driveType == DRIVE_NO_ROOT_DIR) {
        status = ERROR_INVALID_DRIVE;
        return false;
    }
    status = ERROR_SUCCESS;
    return true;
}
#endif

} // namespace

ExecutableFileGuard::~ExecutableFileGuard() noexcept
{
    close();
}

ExecutableFileGuard::ExecutableFileGuard(ExecutableFileGuard &&other) noexcept
    : m_nativeHandle(std::exchange(other.m_nativeHandle, nullptr)),
      m_ancestorHandles(std::move(other.m_ancestorHandles)),
      m_openState(std::exchange(
              other.m_openState, ExecutableFileGuardState::Unavailable)),
      m_requestedPath(std::move(other.m_requestedPath)),
      m_finalPath(std::move(other.m_finalPath)),
      m_identity(other.m_identity),
      m_technicalDetail(std::move(other.m_technicalDetail))
{
    other.m_identity = {};
}

ExecutableFileGuard &ExecutableFileGuard::operator=(
        ExecutableFileGuard &&other) noexcept
{
    if (this == &other)
        return *this;

    close();
    m_nativeHandle = std::exchange(other.m_nativeHandle, nullptr);
    m_openState = std::exchange(
            other.m_openState, ExecutableFileGuardState::Unavailable);
    m_ancestorHandles = std::move(other.m_ancestorHandles);
    m_requestedPath = std::move(other.m_requestedPath);
    m_finalPath = std::move(other.m_finalPath);
    m_identity = other.m_identity;
    m_technicalDetail = std::move(other.m_technicalDetail);
    other.m_identity = {};
    return *this;
}

ExecutableFileGuard ExecutableFileGuard::open(const QString &path) noexcept
{
    ExecutableFileGuard guard;
    try {
        const QString nativeInput = QDir::toNativeSeparators(path);
#ifdef Q_OS_WIN
        guard.m_requestedPath = nativeInput.startsWith(QStringLiteral("\\\\?\\"))
                ? nativeInput
                : QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
#else
        guard.m_requestedPath = QDir::toNativeSeparators(
                QFileInfo(path).absoluteFilePath());
#endif
#ifdef Q_OS_WIN
        DWORD status = ERROR_SUCCESS;
        QString driveRoot;
        if (!localDriveRoot(guard.m_requestedPath, driveRoot, status)) {
            guard.m_technicalDetail =
                    QStringLiteral("可执行文件路径不是受支持的本地 DOS 卷（Win32 %1）：%2")
                            .arg(status)
                            .arg(nativeErrorText(status));
            return guard;
        }

        QStringList segments;
        if (!pathSegments(guard.m_requestedPath, segments, status)) {
            guard.m_technicalDetail =
                    QStringLiteral("可执行文件路径包含不安全的路径段（Win32 %1）：%2")
                            .arg(status)
                            .arg(nativeErrorText(status));
            return guard;
        }

        QString currentPath = driveRoot;
        std::vector<UniqueHandle> ancestors;
        ancestors.reserve(static_cast<size_t>(segments.size() - 1));
        UniqueHandle file;
        for (qsizetype index = 0; index < segments.size(); ++index) {
            if (!currentPath.endsWith(QLatin1Char('\\')))
                currentPath.append(QLatin1Char('\\'));
            currentPath.append(segments.at(index));

            const bool isFinalSegment = index == segments.size() - 1;
            const DWORD desiredAccess = FILE_READ_ATTRIBUTES
                    | (isFinalSegment ? FILE_READ_DATA : 0);
            const DWORD flags = FILE_FLAG_OPEN_REPARSE_POINT
                    | (isFinalSegment ? 0 : FILE_FLAG_BACKUP_SEMANTICS);
            const QString nativePath = extendedNativePath(currentPath);
            UniqueHandle handle(CreateFileW(
                    reinterpret_cast<const wchar_t *>(nativePath.utf16()),
                    desiredAccess,
                    FILE_SHARE_READ,
                    nullptr,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | flags,
                    nullptr));
            if (handle.get() == INVALID_HANDLE_VALUE) {
                status = GetLastError();
                guard.m_openState = missingPathError(status)
                        ? ExecutableFileGuardState::Missing
                        : ExecutableFileGuardState::Unavailable;
                guard.m_technicalDetail =
                        QStringLiteral("无法打开稳定可执行文件路径段“%1”（Win32 %2）：%3")
                                .arg(segments.at(index))
                                .arg(status)
                                .arg(nativeErrorText(status));
                return guard;
            }

            DWORD attributes = 0;
            if (!reparsePointAttributes(handle.get(), attributes, status)) {
                guard.m_technicalDetail =
                        QStringLiteral("无法读取可执行文件路径段属性（Win32 %1）：%2")
                                .arg(status)
                                .arg(nativeErrorText(status));
                return guard;
            }
            if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                guard.m_technicalDetail =
                        QStringLiteral("可执行文件路径不允许包含重解析点：%1")
                                .arg(segments.at(index));
                return guard;
            }
            if (!isFinalSegment && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                guard.m_technicalDetail =
                        QStringLiteral("可执行文件路径段不是目录：%1")
                                .arg(segments.at(index));
                return guard;
            }
            if (isFinalSegment && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                guard.m_technicalDetail =
                        QStringLiteral("路径指向目录，无法作为可执行文件身份");
                return guard;
            }

            if (isFinalSegment)
                file = std::move(handle);
            else
                ancestors.push_back(std::move(handle));
        }

        const ExecutableFileIdentity identity = identityFromHandle(file.get(), status);
        if (!identity.valid) {
            guard.m_technicalDetail = status == ERROR_DIRECTORY
                    ? QStringLiteral("路径指向目录，无法作为可执行文件身份")
                    : QStringLiteral("无法读取可执行文件身份（Win32 %1）：%2")
                              .arg(status)
                              .arg(nativeErrorText(status));
            return guard;
        }

        const QString finalPath = normalizedFinalPath(file.get(), status);
        if (finalPath.isEmpty()) {
            guard.m_technicalDetail =
                    QStringLiteral("无法获取可执行文件的规范最终路径（Win32 %1）：%2")
                            .arg(status)
                            .arg(nativeErrorText(status));
            return guard;
        }
        if (!isLocalDosFinalPath(finalPath, status)) {
            guard.m_technicalDetail =
                    QStringLiteral("可执行文件的最终目标不是受支持的本地 DOS 卷（Win32 %1）：%2")
                            .arg(status)
                            .arg(nativeErrorText(status));
            return guard;
        }

        guard.m_finalPath = finalPath;
        guard.m_identity = identity;
        guard.m_openState = ExecutableFileGuardState::Opened;
        guard.m_nativeHandle = file.release();
        guard.m_ancestorHandles.reserve(ancestors.size());
        for (UniqueHandle &ancestor : ancestors)
            guard.m_ancestorHandles.push_back(ancestor.release());
#else
        guard.m_technicalDetail = QStringLiteral("当前平台不支持 Windows 稳定文件句柄");
#endif
    } catch (const std::exception &error) {
        guard.close();
        guard.m_openState = ExecutableFileGuardState::Unavailable;
        guard.m_finalPath.clear();
        guard.m_identity = {};
        guard.m_technicalDetail = QStringLiteral("打开稳定可执行文件句柄时发生异常：%1")
                                          .arg(QString::fromUtf8(error.what()));
    } catch (...) {
        guard.close();
        guard.m_openState = ExecutableFileGuardState::Unavailable;
        guard.m_finalPath.clear();
        guard.m_identity = {};
        guard.m_technicalDetail = QStringLiteral("打开稳定可执行文件句柄时发生未知异常");
    }
    return guard;
}

bool ExecutableFileGuard::isOpen() const noexcept
{
    return m_openState == ExecutableFileGuardState::Opened
            && m_nativeHandle != nullptr && m_identity.valid
            && !m_finalPath.isEmpty();
}

ExecutableFileGuardState ExecutableFileGuard::openState() const noexcept
{
    return m_openState;
}

const QString &ExecutableFileGuard::requestedPath() const noexcept
{
    return m_requestedPath;
}

const QString &ExecutableFileGuard::finalPath() const noexcept
{
    return m_finalPath;
}

const ExecutableFileIdentity &ExecutableFileGuard::identity() const noexcept
{
    return m_identity;
}

const QString &ExecutableFileGuard::technicalDetail() const noexcept
{
    return m_technicalDetail;
}

void *ExecutableFileGuard::nativeHandle() const noexcept
{
    return m_nativeHandle;
}

void ExecutableFileGuard::close() noexcept
{
#ifdef Q_OS_WIN
    if (m_nativeHandle != nullptr)
        CloseHandle(static_cast<HANDLE>(m_nativeHandle));
    for (void *handle : m_ancestorHandles) {
        if (handle != nullptr)
            CloseHandle(static_cast<HANDLE>(handle));
    }
#endif
    m_nativeHandle = nullptr;
    m_ancestorHandles.clear();
}

ExecutableFileIdentityResult ExecutableFileIdentityReader::read(
        const QString &path) noexcept
{
    try {
        const ExecutableFileGuard guard = ExecutableFileGuard::open(path);
        return {guard.identity(), guard.technicalDetail()};
    } catch (const std::exception &error) {
        return {
            {},
            QStringLiteral("读取文件身份时发生异常：%1")
                    .arg(QString::fromUtf8(error.what()))
        };
    } catch (...) {
        return {{}, QStringLiteral("读取文件身份时发生未知异常")};
    }
}

} // namespace wam::platform::windows
