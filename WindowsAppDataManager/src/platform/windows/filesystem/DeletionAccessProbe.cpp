#include "DeletionAccessProbe.h"

#include "StablePathIdentity.h"

#include <QDir>
#include <QFileInfo>

#include <array>
#include <exception>
#include <filesystem>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace wam::platform::windows {
namespace {

QString pathToQString(const std::filesystem::path &path)
{
#ifdef Q_OS_WIN
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromStdString(path.string());
#endif
}

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

QString extendedNativePath(const QString &path)
{
    QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    if (native.startsWith(QStringLiteral("\\\\?\\")))
        return native;
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

bool canOpenForDelete(const QString &path, DWORD &status)
{
    const QString native = extendedNativePath(path);
    UniqueHandle handle(CreateFileW(
            reinterpret_cast<const wchar_t *>(native.utf16()),
            DELETE | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
            nullptr));
    if (handle.get() == INVALID_HANDLE_VALUE) {
        status = GetLastError();
        return false;
    }

    BY_HANDLE_FILE_INFORMATION information {};
    if (!GetFileInformationByHandle(handle.get(), &information)) {
        status = GetLastError();
        return false;
    }
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        status = ERROR_REPARSE_TAG_INVALID;
        return false;
    }

    status = ERROR_SUCCESS;
    return true;
}

DeletionAccessResult probeWindowsPath(const QString &root)
{
    DeletionAccessResult result;
    result.supported = true;

    const StablePathIdentityResult rootIdentity =
            StablePathIdentityReader::read(root);
    if (rootIdentity.state != StablePathState::Present
            || !rootIdentity.identity.valid || !rootIdentity.identity.directory) {
        result.path = QDir::toNativeSeparators(root);
        result.nativeError = rootIdentity.nativeError != 0
                ? rootIdentity.nativeError : ERROR_DIRECTORY;
        result.technicalDetail = rootIdentity.technicalDetail.isEmpty()
                ? QStringLiteral("删除访问探测只支持稳定的本地目录")
                : rootIdentity.technicalDetail;
        return result;
    }

    DWORD status = ERROR_SUCCESS;
    if (!canOpenForDelete(rootIdentity.finalPath, status)) {
        result.path = QDir::toNativeSeparators(root);
        result.nativeError = status;
        result.technicalDetail = QStringLiteral(
                "无法取得删除访问权限（Win32 %1）：%2")
                                         .arg(status)
                                         .arg(nativeErrorText(status));
        return result;
    }

    const std::filesystem::path rootPath(rootIdentity.finalPath.toStdWString());
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
            rootPath, std::filesystem::directory_options::none, error);
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        result.path = root;
        result.nativeError = static_cast<quint32>(error.value());
        result.technicalDetail = QString::fromStdString(error.message());
        return result;
    }

    while (iterator != end) {
        const std::filesystem::path path = iterator->path();
        const QString itemPath = pathToQString(path);
        if (!canOpenForDelete(itemPath, status)) {
            result.path = itemPath;
            result.nativeError = status;
            result.technicalDetail = status == ERROR_REPARSE_TAG_INVALID
                    ? QStringLiteral("删除目标中不允许包含重解析点")
                    : QStringLiteral(
                              "文件被锁定或没有删除访问权限（Win32 %1）：%2")
                              .arg(status)
                              .arg(nativeErrorText(status));
            return result;
        }

        iterator.increment(error);
        if (error) {
            result.path = itemPath;
            result.nativeError = static_cast<quint32>(error.value());
            result.technicalDetail = QString::fromStdString(error.message());
            return result;
        }
    }

    result.available = true;
    return result;
}
#endif

} // namespace

DeletionAccessResult DeletionAccessProbe::probe(const QString &path) noexcept
{
    try {
#ifdef Q_OS_WIN
        return probeWindowsPath(path);
#else
        DeletionAccessResult result;
        result.technicalDetail = QStringLiteral("当前平台不支持删除访问探测");
        return result;
#endif
    } catch (const std::exception &error) {
        DeletionAccessResult result;
#ifdef Q_OS_WIN
        result.supported = true;
#endif
        result.technicalDetail = QStringLiteral("探测删除访问权限时发生异常：%1")
                                         .arg(QString::fromUtf8(error.what()));
        return result;
    } catch (...) {
        DeletionAccessResult result;
#ifdef Q_OS_WIN
        result.supported = true;
#endif
        result.technicalDetail = QStringLiteral("探测删除访问权限时发生未知异常");
        return result;
    }
}

} // namespace wam::platform::windows
