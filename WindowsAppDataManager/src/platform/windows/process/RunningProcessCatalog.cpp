#include "RunningProcessCatalog.h"

#include <QDir>

#include <array>
#include <exception>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <TlHelp32.h>
#endif

namespace wam::platform::windows {
namespace {

#ifdef Q_OS_WIN
constexpr qsizetype maximumReportedIssues = 20;
constexpr DWORD maximumImagePathCharacters = 32768;

class UniqueHandle final {
public:
    explicit UniqueHandle(HANDLE handle = nullptr) noexcept
        : m_handle(handle)
    {
    }

    ~UniqueHandle()
    {
        if (isValid())
            CloseHandle(m_handle);
    }

    UniqueHandle(const UniqueHandle &) = delete;
    UniqueHandle &operator=(const UniqueHandle &) = delete;

    UniqueHandle(UniqueHandle &&other) noexcept
        : m_handle(std::exchange(other.m_handle, nullptr))
    {
    }

    UniqueHandle &operator=(UniqueHandle &&other) noexcept
    {
        if (this == &other)
            return *this;
        if (isValid())
            CloseHandle(m_handle);
        m_handle = std::exchange(other.m_handle, nullptr);
        return *this;
    }

    [[nodiscard]] bool isValid() const noexcept
    {
        return m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE;
    }

    [[nodiscard]] HANDLE get() const noexcept
    {
        return m_handle;
    }

private:
    HANDLE m_handle = nullptr;
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

void appendIssue(RunningProcessQueryResult &result,
                 quint32 processId,
                 QString processName,
                 DWORD nativeError,
                 QString detail = {})
{
    if (result.issues.size() >= maximumReportedIssues)
        return;
    if (detail.isEmpty())
        detail = nativeErrorText(nativeError);
    result.issues.append({processId, nativeError,
                          std::move(processName), std::move(detail)});
}

bool isSystemPseudoProcess(quint32 processId, const QString &imageName)
{
    if (processId == 0)
        return true;
    return imageName.compare(QStringLiteral("System"), Qt::CaseInsensitive) == 0
            || imageName.compare(QStringLiteral("System Idle Process"),
                                 Qt::CaseInsensitive) == 0
            || imageName.compare(QStringLiteral("Registry"),
                                 Qt::CaseInsensitive) == 0
            || imageName.compare(QStringLiteral("Memory Compression"),
                                 Qt::CaseInsensitive) == 0
            || imageName.compare(QStringLiteral("Secure System"),
                                 Qt::CaseInsensitive) == 0;
}

RunningProcessQueryResult queryWindowsProcesses()
{
    RunningProcessQueryResult result;
    result.supported = true;

    UniqueHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.isValid()) {
        const DWORD status = GetLastError();
        appendIssue(result, 0, {}, status,
                    QStringLiteral("无法创建运行进程快照：%1")
                            .arg(nativeErrorText(status)));
        return result;
    }

    PROCESSENTRY32W entry {};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) {
        const DWORD status = GetLastError();
        if (status == ERROR_NO_MORE_FILES) {
            result.available = true;
            result.complete = true;
        } else {
            appendIssue(result, 0, {}, status,
                        QStringLiteral("无法读取运行进程快照：%1")
                                .arg(nativeErrorText(status)));
        }
        return result;
    }

    result.available = true;
    result.complete = true;
    quint64 inaccessibleProcesses = 0;
    QVector<wchar_t> pathBuffer(
            static_cast<qsizetype>(maximumImagePathCharacters));
    do {
        const quint32 processId = entry.th32ProcessID;
        const QString imageName = QString::fromWCharArray(entry.szExeFile).trimmed();
        if (isSystemPseudoProcess(processId, imageName))
            continue;

        UniqueHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                         FALSE, processId));
        if (!process.isValid()) {
            const DWORD status = GetLastError();
            if (status == ERROR_INVALID_PARAMETER)
                continue;
            result.complete = false;
            if (status == ERROR_ACCESS_DENIED) {
                ++inaccessibleProcesses;
            } else {
                appendIssue(result, processId, imageName, status);
            }
            continue;
        }

        DWORD pathLength = static_cast<DWORD>(pathBuffer.size());
        if (!QueryFullProcessImageNameW(process.get(), 0,
                                        pathBuffer.data(), &pathLength)) {
            const DWORD status = GetLastError();
            if (status == ERROR_INVALID_PARAMETER)
                continue;
            result.complete = false;
            if (status == ERROR_ACCESS_DENIED) {
                ++inaccessibleProcesses;
            } else {
                appendIssue(result, processId, imageName, status);
            }
            continue;
        }

        QString imagePath = QString::fromWCharArray(
                pathBuffer.constData(), static_cast<qsizetype>(pathLength));
        imagePath = QDir::toNativeSeparators(
                QDir::cleanPath(QDir::fromNativeSeparators(imagePath)));
        if (imagePath.isEmpty()) {
            result.complete = false;
            appendIssue(result, processId, imageName, ERROR_INVALID_DATA,
                        QStringLiteral("进程映像路径为空"));
            continue;
        }
        result.processes.append({processId, imageName, imagePath});
    } while (Process32NextW(snapshot.get(), &entry));

    const DWORD iterationStatus = GetLastError();
    if (iterationStatus != ERROR_NO_MORE_FILES) {
        result.complete = false;
        appendIssue(result, 0, {}, iterationStatus,
                    QStringLiteral("运行进程快照枚举提前终止：%1")
                            .arg(nativeErrorText(iterationStatus)));
    }

    if (inaccessibleProcesses > 0) {
        appendIssue(
                result, 0, {}, ERROR_ACCESS_DENIED,
                QStringLiteral("%1 个受保护进程不允许读取映像路径")
                        .arg(inaccessibleProcesses));
    }
    return result;
}
#endif

} // namespace

RunningProcessQueryResult RunningProcessCatalog::query() noexcept
{
    try {
#ifdef Q_OS_WIN
        return queryWindowsProcesses();
#else
        return {};
#endif
    } catch (const std::exception &error) {
        RunningProcessQueryResult result;
#ifdef Q_OS_WIN
        result.supported = true;
#endif
        result.issues.append({0, 0, {},
                              QStringLiteral("枚举运行进程时发生异常：%1")
                                      .arg(QString::fromUtf8(error.what()))});
        return result;
    } catch (...) {
        RunningProcessQueryResult result;
#ifdef Q_OS_WIN
        result.supported = true;
#endif
        result.issues.append({0, 0, {},
                              QStringLiteral("枚举运行进程时发生未知异常")});
        return result;
    }
}

} // namespace wam::platform::windows
