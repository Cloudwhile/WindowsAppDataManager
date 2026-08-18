#include "StablePathIdentity.h"

#include <QDir>
#include <QStringList>

#include <algorithm>
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
    explicit UniqueHandle(HANDLE value = INVALID_HANDLE_VALUE) noexcept
        : m_value(value)
    {
    }

    ~UniqueHandle()
    {
        if (m_value != INVALID_HANDLE_VALUE)
            CloseHandle(m_value);
    }

    UniqueHandle(const UniqueHandle &) = delete;
    UniqueHandle &operator=(const UniqueHandle &) = delete;

    UniqueHandle(UniqueHandle &&other) noexcept
        : m_value(std::exchange(other.m_value, INVALID_HANDLE_VALUE))
    {
    }

    UniqueHandle &operator=(UniqueHandle &&other) noexcept
    {
        if (this == &other)
            return *this;
        if (m_value != INVALID_HANDLE_VALUE)
            CloseHandle(m_value);
        m_value = std::exchange(other.m_value, INVALID_HANDLE_VALUE);
        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept { return m_value; }

private:
    HANDLE m_value = INVALID_HANDLE_VALUE;
};

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

QString extendedNativePath(const QString &path)
{
    QString native = QDir::toNativeSeparators(path);
    if (native.startsWith(QStringLiteral("\\\\?\\")))
        return native;
    return QStringLiteral("\\\\?\\") + native;
}

bool missingPathError(DWORD status) noexcept
{
    return status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND;
}

quint64 combinedValue(DWORD high, DWORD low) noexcept
{
    return (static_cast<quint64>(high) << 32U) | static_cast<quint64>(low);
}

bool isLocalAbsolutePath(const QString &path)
{
    if (path.size() < 3 || path.at(1) != QLatin1Char(':')
            || (path.at(2) != QLatin1Char('\\')
                && path.at(2) != QLatin1Char('/'))) {
        return false;
    }
    const ushort drive = path.at(0).unicode();
    return (drive >= static_cast<ushort>('A') && drive <= static_cast<ushort>('Z'))
            || (drive >= static_cast<ushort>('a') && drive <= static_cast<ushort>('z'));
}

StablePathIdentityResult readWindowsIdentity(const QString &path)
{
    StablePathIdentityResult result;
    const QString requestedPath = path.trimmed();
    if (requestedPath.isEmpty() || requestedPath != path
            || requestedPath.contains(QChar::Null)
            || !QDir::isAbsolutePath(QDir::fromNativeSeparators(requestedPath))) {
        result.nativeError = ERROR_INVALID_NAME;
        result.technicalDetail = QStringLiteral(
                "路径必须是没有首尾空白的本地绝对路径");
        return result;
    }

    const QString rawPath = QDir::fromNativeSeparators(requestedPath);
    if (!isLocalAbsolutePath(rawPath)) {
        result.nativeError = ERROR_INVALID_NAME;
        result.technicalDetail = QStringLiteral("路径不是受支持的本地绝对路径");
        return result;
    }

    const QStringList rawSegments = rawPath.sliced(3).split(
            QLatin1Char('/'), Qt::SkipEmptyParts);
    if (std::any_of(rawSegments.cbegin(), rawSegments.cend(),
                    [](const QString &segment) {
        return segment == QStringLiteral(".") || segment == QStringLiteral("..");
    })) {
        result.nativeError = ERROR_INVALID_NAME;
        result.technicalDetail = QStringLiteral("路径包含不安全的跳转段");
        return result;
    }

    const QString absolutePath = QDir::toNativeSeparators(
            QDir::cleanPath(rawPath));
    const QStringList segments = QDir::fromNativeSeparators(
            absolutePath.sliced(3)).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (segments.isEmpty()) {
        result.nativeError = ERROR_INVALID_NAME;
        result.technicalDetail = QStringLiteral("路径不能指向卷根目录");
        return result;
    }

    QString currentPath = absolutePath.left(3);
    UniqueHandle finalHandle;
    for (qsizetype index = 0; index < segments.size(); ++index) {
        const QString &segment = segments.at(index);
        if (!currentPath.endsWith(QLatin1Char('\\')))
            currentPath.append(QLatin1Char('\\'));
        currentPath.append(segment);

        UniqueHandle handle(CreateFileW(
                reinterpret_cast<const wchar_t *>(
                        extendedNativePath(currentPath).utf16()),
                FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                nullptr));
        if (handle.get() == INVALID_HANDLE_VALUE) {
            const DWORD status = GetLastError();
            result.state = missingPathError(status)
                    ? StablePathState::Missing : StablePathState::Unavailable;
            result.nativeError = status;
            result.technicalDetail = QStringLiteral(
                    "无法打开稳定路径段“%1”（Win32 %2）：%3")
                                             .arg(segment)
                                             .arg(status)
                                             .arg(nativeErrorText(status));
            return result;
        }

        BY_HANDLE_FILE_INFORMATION information {};
        if (!GetFileInformationByHandle(handle.get(), &information)) {
            const DWORD status = GetLastError();
            result.nativeError = status;
            result.technicalDetail = QStringLiteral(
                    "无法读取路径身份（Win32 %1）：%2")
                                             .arg(status)
                                             .arg(nativeErrorText(status));
            return result;
        }
        if ((information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            result.nativeError = ERROR_REPARSE_TAG_INVALID;
            result.technicalDetail = QStringLiteral("稳定路径不允许包含重解析点：%1")
                                             .arg(segment);
            return result;
        }

        const bool finalSegment = index == segments.size() - 1;
        if (!finalSegment
                && (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            result.nativeError = ERROR_DIRECTORY;
            result.technicalDetail = QStringLiteral("路径中间段不是目录：%1")
                                             .arg(segment);
            return result;
        }
        if (finalSegment) {
            result.identity.valid = true;
            result.identity.volumeSerialNumber = information.dwVolumeSerialNumber;
            result.identity.fileIndex = combinedValue(
                    information.nFileIndexHigh, information.nFileIndexLow);
            result.identity.directory =
                    (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            finalHandle = std::move(handle);
        }
    }

    constexpr DWORD finalPathFlags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
    const DWORD required = GetFinalPathNameByHandleW(
            finalHandle.get(), nullptr, 0, finalPathFlags);
    if (required == 0) {
        const DWORD status = GetLastError();
        result.identity = {};
        result.nativeError = status;
        result.technicalDetail = QStringLiteral(
                "无法获取最终路径（Win32 %1）：%2")
                                         .arg(status)
                                         .arg(nativeErrorText(status));
        return result;
    }
    std::vector<wchar_t> buffer(static_cast<size_t>(required) + 1U);
    const DWORD written = GetFinalPathNameByHandleW(
            finalHandle.get(), buffer.data(),
            static_cast<DWORD>(buffer.size()), finalPathFlags);
    if (written == 0 || written >= static_cast<DWORD>(buffer.size())) {
        const DWORD status = written == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
        result.identity = {};
        result.nativeError = status;
        result.technicalDetail = QStringLiteral(
                "无法读取最终路径（Win32 %1）：%2")
                                         .arg(status)
                                         .arg(nativeErrorText(status));
        return result;
    }

    result.finalPath = QString::fromWCharArray(
            buffer.data(), static_cast<qsizetype>(written));
    if (result.finalPath.startsWith(QStringLiteral("\\\\?\\")))
        result.finalPath.remove(0, 4);
    result.finalPath = QDir::toNativeSeparators(result.finalPath);
    result.state = StablePathState::Present;
    return result;
}
#endif

} // namespace

StablePathIdentityResult StablePathIdentityReader::read(
        const QString &path) noexcept
{
    try {
#ifdef Q_OS_WIN
        return readWindowsIdentity(path);
#else
        StablePathIdentityResult result;
        result.technicalDetail = QStringLiteral("当前平台不支持稳定路径身份读取");
        return result;
#endif
    } catch (const std::exception &error) {
        StablePathIdentityResult result;
        result.technicalDetail = QStringLiteral("读取稳定路径身份时发生异常：%1")
                                         .arg(QString::fromUtf8(error.what()));
        return result;
    } catch (...) {
        StablePathIdentityResult result;
        result.technicalDetail = QStringLiteral("读取稳定路径身份时发生未知异常");
        return result;
    }
}

} // namespace wam::platform::windows
