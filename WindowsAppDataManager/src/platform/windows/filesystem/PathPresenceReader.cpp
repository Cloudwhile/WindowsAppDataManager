#include "PathPresenceReader.h"

#include <QDir>
#include <QFileInfo>

#include <array>
#include <exception>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace wam::platform::windows {
namespace {

#ifdef Q_OS_WIN
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

bool missingPathError(DWORD status) noexcept
{
    return status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND;
}

QString extendedNativePath(const QString &path)
{
    QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    if (native.startsWith(QStringLiteral("\\\\?\\")) || native.size() < 248)
        return native;
    if (native.startsWith(QStringLiteral("\\\\")))
        return QStringLiteral("\\\\?\\UNC\\") + native.sliced(2);
    return QStringLiteral("\\\\?\\") + native;
}
#endif

} // namespace

PathPresenceResult PathPresenceReader::read(const QString &path) noexcept
{
    PathPresenceResult result;
    result.supported = true;
    try {
        if (path.trimmed().isEmpty()) {
            result.technicalDetail = QStringLiteral("路径为空，无法检查存在状态");
            return result;
        }

#ifdef Q_OS_WIN
        const QString nativePath = extendedNativePath(path);
        const DWORD attributes = GetFileAttributesW(
                reinterpret_cast<const wchar_t *>(nativePath.utf16()));
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD status = GetLastError();
            result.nativeError = status;
            if (missingPathError(status)) {
                result.state = PathPresenceState::Missing;
            } else {
                result.technicalDetail = QStringLiteral("无法读取路径属性（Win32 %1）：%2")
                                                 .arg(status)
                                                 .arg(nativeErrorText(status));
            }
            return result;
        }

        result.state = PathPresenceState::Present;
        result.directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
        const QFileInfo information(path);
        if (!information.exists()) {
            result.state = PathPresenceState::Missing;
            return result;
        }
        result.state = PathPresenceState::Present;
        result.directory = information.isDir();
#endif
    } catch (const std::exception &error) {
        result.state = PathPresenceState::Unavailable;
        result.technicalDetail = QStringLiteral("检查路径存在状态时发生异常：%1")
                                         .arg(QString::fromUtf8(error.what()));
    } catch (...) {
        result.state = PathPresenceState::Unavailable;
        result.technicalDetail = QStringLiteral("检查路径存在状态时发生未知异常");
    }
    return result;
}

} // namespace wam::platform::windows
