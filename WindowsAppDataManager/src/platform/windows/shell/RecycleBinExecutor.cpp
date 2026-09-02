#include "RecycleBinExecutor.h"

#include "../filesystem/StablePathIdentity.h"

#include <QDir>

#include <array>
#include <exception>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <ShObjIdl.h>
#endif

namespace wam::platform::windows {
namespace {

#ifdef Q_OS_WIN
QString nativeErrorText(HRESULT status)
{
    DWORD messageStatus = static_cast<DWORD>(status);
    if (HRESULT_FACILITY(status) == FACILITY_WIN32)
        messageStatus = HRESULT_CODE(status);

    std::array<wchar_t, 512> buffer {};
    const DWORD length = FormatMessageW(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            messageStatus,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            nullptr);
    if (length == 0)
        return QStringLiteral("HRESULT 0x%1")
                .arg(static_cast<quint32>(status), 8, 16, QLatin1Char('0'));
    return QString::fromWCharArray(
            buffer.data(), static_cast<qsizetype>(length)).trimmed();
}

class ComApartment final {
public:
    ComApartment()
        : m_status(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))
    {
    }

    ~ComApartment()
    {
        if (m_status == S_OK || m_status == S_FALSE)
            CoUninitialize();
    }

    [[nodiscard]] HRESULT status() const noexcept { return m_status; }
    [[nodiscard]] bool available() const noexcept
    {
        return m_status == S_OK || m_status == S_FALSE;
    }

private:
    HRESULT m_status = E_FAIL;
};

template<typename T>
class ComPointer final {
public:
    ~ComPointer()
    {
        if (m_value)
            m_value->Release();
    }

    ComPointer(const ComPointer &) = delete;
    ComPointer &operator=(const ComPointer &) = delete;
    ComPointer() = default;

    [[nodiscard]] T **address() noexcept { return &m_value; }
    [[nodiscard]] T *get() const noexcept { return m_value; }

private:
    T *m_value = nullptr;
};

QString normalizedPathKey(QString path)
{
    path = QDir::cleanPath(QDir::fromNativeSeparators(path));
    return path.toCaseFolded();
}

bool matchesExpectedIdentity(
        const CleanupCandidateInfo &candidate,
        const StablePathIdentityResult &identity)
{
    return candidate.identityValid && candidate.directory
            && identity.state == StablePathState::Present
            && identity.identity.valid && identity.identity.directory
            && identity.identity.volumeSerialNumber == candidate.volumeSerialNumber
            && identity.identity.fileIndex == candidate.fileIndex
            && normalizedPathKey(identity.finalPath)
                    == normalizedPathKey(candidate.path);
}

services::CleanupExecutionOutcome recycleWindowsPath(
        const CleanupCandidateInfo &candidate)
{
    const StablePathIdentityResult identity = StablePathIdentityReader::read(
            candidate.path);
    if (!matchesExpectedIdentity(candidate, identity)) {
        return {
            false, false, false, identity.nativeError,
            QStringLiteral("待清理目录身份已发生变化"),
            identity.technicalDetail
        };
    }

    ComApartment apartment;
    if (!apartment.available()) {
        return {
            false, false, false, static_cast<quint32>(apartment.status()),
            QStringLiteral("无法初始化 Windows 文件操作"),
            nativeErrorText(apartment.status())
        };
    }

    ComPointer<IFileOperation> operation;
    HRESULT status = CoCreateInstance(
            CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(operation.address()));
    if (FAILED(status)) {
        return {
            false, false, false, static_cast<quint32>(status),
            QStringLiteral("无法创建 Windows 回收站操作"),
            nativeErrorText(status)
        };
    }

    constexpr DWORD flags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION
            | FOF_NOERRORUI | FOF_SILENT | FOF_NO_CONNECTED_ELEMENTS
            | FOF_NORECURSEREPARSE | FOFX_RECYCLEONDELETE
            | FOFX_EARLYFAILURE;
    status = operation.get()->SetOperationFlags(flags);
    if (FAILED(status)) {
        return {
            false, false, false, static_cast<quint32>(status),
            QStringLiteral("无法配置可恢复清理操作"),
            nativeErrorText(status)
        };
    }

    ComPointer<IShellItem> item;
    status = SHCreateItemFromParsingName(
            reinterpret_cast<const wchar_t *>(identity.finalPath.utf16()),
            nullptr, IID_PPV_ARGS(item.address()));
    if (FAILED(status)) {
        return {
            false, false, false, static_cast<quint32>(status),
            QStringLiteral("无法打开待清理项目"),
            nativeErrorText(status)
        };
    }

    const StablePathIdentityResult queuedIdentity = StablePathIdentityReader::read(
            candidate.path);
    if (!matchesExpectedIdentity(candidate, queuedIdentity)) {
        return {
            false, false, false, queuedIdentity.nativeError,
            QStringLiteral("待清理目录在创建回收站操作时发生变化"),
            queuedIdentity.technicalDetail
        };
    }

    status = operation.get()->DeleteItem(item.get(), nullptr);
    if (FAILED(status)) {
        return {
            false, false, false, static_cast<quint32>(status),
            QStringLiteral("无法加入回收站操作队列"),
            nativeErrorText(status)
        };
    }
    const HRESULT performStatus = operation.get()->PerformOperations();
    BOOL aborted = FALSE;
    const HRESULT abortedStatus =
            operation.get()->GetAnyOperationsAborted(&aborted);
    if (performStatus == COPYENGINE_E_USER_CANCELLED) {
        return {
            false, true, true, static_cast<quint32>(performStatus),
            QStringLiteral("回收站操作已取消"),
            nativeErrorText(performStatus)
        };
    }
    if (FAILED(performStatus)) {
        return {
            false, false, false, static_cast<quint32>(performStatus),
            QStringLiteral("移动到回收站失败"),
            nativeErrorText(performStatus)
        };
    }
    if (FAILED(abortedStatus)) {
        return {
            false, true, false, static_cast<quint32>(abortedStatus),
            QStringLiteral("无法确认回收站操作结果"),
            nativeErrorText(abortedStatus)
        };
    }
    if (aborted != FALSE) {
        return {
            false, true, true, 0,
            QStringLiteral("回收站操作已中止"), {}
        };
    }

    return {
        true, true, false, 0,
        QStringLiteral("已移动到回收站"), {}
    };
}
#endif

} // namespace

services::CleanupExecutionOutcome RecycleBinExecutor::moveToRecycleBin(
        const CleanupCandidateInfo &candidate)
{
    try {
#ifdef Q_OS_WIN
        return recycleWindowsPath(candidate);
#else
        return {false, false, false, 0,
                QStringLiteral("当前平台不支持回收站清理"), {}};
#endif
    } catch (const std::exception &error) {
        return {false, false, false, 0,
                QStringLiteral("移动到回收站时发生异常"),
                QString::fromUtf8(error.what())};
    } catch (...) {
        return {false, false, false, 0,
                QStringLiteral("移动到回收站时发生未知异常"), {}};
    }
}

} // namespace wam::platform::windows
