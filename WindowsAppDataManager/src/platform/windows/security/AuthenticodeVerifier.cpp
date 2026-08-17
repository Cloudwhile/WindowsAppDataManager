#include "AuthenticodeVerifier.h"

#include <QVector>

#include <array>
#include <exception>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Softpub.h>
#include <Wincrypt.h>
#include <Wintrust.h>
#endif

namespace wam::platform::windows {
namespace {

#ifdef Q_OS_WIN
QString statusText(LONG status)
{
    std::array<wchar_t, 512> buffer {};
    const DWORD length = FormatMessageW(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            static_cast<DWORD>(status),
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            nullptr);
    const QString message = length == 0
            ? QStringLiteral("未提供系统错误文本")
            : QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(length)).trimmed();
    return QStringLiteral("HRESULT 0x%1：%2")
            .arg(static_cast<quint32>(status), 8, 16, QLatin1Char('0'))
            .arg(message);
}

QString signerPublisher(const WINTRUST_DATA &trustData)
{
    CRYPT_PROVIDER_DATA *providerData = WTHelperProvDataFromStateData(
            trustData.hWVTStateData);
    if (providerData == nullptr)
        return {};
    CRYPT_PROVIDER_SGNR *signer = WTHelperGetProvSignerFromChain(
            providerData, 0, FALSE, 0);
    if (signer == nullptr || signer->csCertChain == 0
            || signer->pasCertChain == nullptr
            || signer->pasCertChain[0].pCert == nullptr) {
        return {};
    }

    PCCERT_CONTEXT certificate = signer->pasCertChain[0].pCert;
    const DWORD characterCount = CertGetNameStringW(
            certificate,
            CERT_NAME_SIMPLE_DISPLAY_TYPE,
            0,
            nullptr,
            nullptr,
            0);
    if (characterCount <= 1)
        return {};

    QVector<wchar_t> buffer(static_cast<qsizetype>(characterCount));
    const DWORD written = CertGetNameStringW(
            certificate,
            CERT_NAME_SIMPLE_DISPLAY_TYPE,
            0,
            nullptr,
            buffer.data(),
            characterCount);
    if (written <= 1)
        return {};
    return QString::fromWCharArray(
            buffer.constData(), static_cast<qsizetype>(written - 1)).trimmed();
}

bool pathStillReferencesGuardedFile(const ExecutableFileGuard &guard,
                                    QString *technicalDetail)
{
    const ExecutableFileGuard reopened = ExecutableFileGuard::open(
            guard.finalPath());
    if (!reopened.isOpen()) {
        if (technicalDetail) {
            *technicalDetail = reopened.technicalDetail().isEmpty()
                    ? QStringLiteral("无法重新确认 Authenticode 验证期间的文件身份")
                    : reopened.technicalDetail();
        }
        return false;
    }
    if (reopened.identity() != guard.identity()) {
        if (technicalDetail)
            *technicalDetail = QStringLiteral("Authenticode 验证期间路径指向了另一文件");
        return false;
    }
    return true;
}

class TrustState final {
public:
    TrustState(GUID action, WINTRUST_DATA &data) noexcept
        : m_action(action),
          m_data(data)
    {
    }

    ~TrustState()
    {
        if (m_data.hWVTStateData == nullptr)
            return;
        m_data.dwStateAction = WTD_STATEACTION_CLOSE;
        (void)WinVerifyTrust(nullptr, &m_action, &m_data);
    }

    TrustState(const TrustState &) = delete;
    TrustState &operator=(const TrustState &) = delete;

private:
    GUID m_action;
    WINTRUST_DATA &m_data;
};

bool statusFromWin32(LONG status, DWORD error)
{
    return status == static_cast<LONG>(HRESULT_FROM_WIN32(error));
}

AuthenticodeVerificationResult verifyWindowsFile(
        const ExecutableFileGuard &guard)
{
    if (!guard.isOpen()) {
        AuthenticodeVerificationResult unavailable;
        unavailable.technicalDetail = guard.technicalDetail().isEmpty()
                ? QStringLiteral("无法建立 Authenticode 验证所需的稳定文件句柄")
                : QStringLiteral("无法建立 Authenticode 验证所需的稳定文件句柄：%1")
                          .arg(guard.technicalDetail());
        return unavailable;
    }

    const QString &nativePath = guard.finalPath();
    WINTRUST_FILE_INFO fileInfo {};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = reinterpret_cast<const wchar_t *>(nativePath.utf16());
    fileInfo.hFile = static_cast<HANDLE>(guard.nativeHandle());

    WINTRUST_DATA trustData {};
    trustData.cbStruct = sizeof(trustData);
    trustData.dwUIChoice = WTD_UI_NONE;
    trustData.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    trustData.dwUnionChoice = WTD_CHOICE_FILE;
    trustData.pFile = &fileInfo;
    trustData.dwStateAction = WTD_STATEACTION_VERIFY;
    trustData.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL
            | WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
    trustData.dwUIContext = WTD_UICONTEXT_EXECUTE;

    QString identityDetail;
    if (!pathStillReferencesGuardedFile(guard, &identityDetail)) {
        AuthenticodeVerificationResult unavailable;
        unavailable.fileIdentity = guard.identity();
        unavailable.technicalDetail = identityDetail;
        return unavailable;
    }

    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    SetLastError(ERROR_SUCCESS);
    const LONG nativeStatus = WinVerifyTrust(nullptr, &action, &trustData);
    const DWORD secondaryStatus = nativeStatus == TRUST_E_NOSIGNATURE
            ? GetLastError() : ERROR_SUCCESS;
    TrustState state(action, trustData);
    if (!pathStillReferencesGuardedFile(guard, &identityDetail)) {
        AuthenticodeVerificationResult unavailable;
        unavailable.fileIdentity = guard.identity();
        unavailable.identityStable = false;
        unavailable.nativeStatus = static_cast<quint32>(nativeStatus);
        unavailable.technicalDetail = identityDetail;
        return unavailable;
    }

    AuthenticodeVerificationResult result;
    result.status = classifyAuthenticodeStatus(
            static_cast<quint32>(nativeStatus),
            static_cast<quint32>(secondaryStatus));
    result.nativeStatus = static_cast<quint32>(nativeStatus);
    result.publisher = signerPublisher(trustData);
    if (nativeStatus != ERROR_SUCCESS)
        result.technicalDetail = statusText(nativeStatus);
    if (result.status == AuthenticodeVerificationStatus::Unavailable
            && nativeStatus == TRUST_E_NOSIGNATURE
            && secondaryStatus != ERROR_SUCCESS) {
        result.technicalDetail += QStringLiteral("；底层状态：%1")
                                          .arg(statusText(
                                                  static_cast<LONG>(secondaryStatus)));
    }
    result.fileIdentity = guard.identity();
    result.identityStable = true;
    return result;
}
#endif

} // namespace

AuthenticodeVerificationStatus classifyAuthenticodeStatus(
        quint32 nativeStatus,
        quint32 secondaryStatus) noexcept
{
#ifdef Q_OS_WIN
    const LONG status = static_cast<LONG>(nativeStatus);
    if (status == ERROR_SUCCESS)
        return AuthenticodeVerificationStatus::Trusted;
    if (status == TRUST_E_NOSIGNATURE) {
        const LONG detail = static_cast<LONG>(secondaryStatus);
        if (detail == TRUST_E_NOSIGNATURE
                || detail == TRUST_E_SUBJECT_FORM_UNKNOWN
                || detail == TRUST_E_PROVIDER_UNKNOWN) {
            return AuthenticodeVerificationStatus::Unsigned;
        }
        return AuthenticodeVerificationStatus::Unavailable;
    }

    if (status == TRUST_E_BAD_DIGEST
            || status == TRUST_E_CERT_SIGNATURE
            || status == TRUST_E_NO_SIGNER_CERT
            || status == TRUST_E_COUNTER_SIGNER
            || status == TRUST_E_TIME_STAMP
            || status == TRUST_E_EXPLICIT_DISTRUST
            || status == TRUST_E_SUBJECT_NOT_TRUSTED
            || status == TRUST_E_BASIC_CONSTRAINTS
            || status == TRUST_E_FINANCIAL_CRITERIA
            || status == CERT_E_EXPIRED
            || status == CERT_E_VALIDITYPERIODNESTING
            || status == CERT_E_ROLE
            || status == CERT_E_PATHLENCONST
            || status == CERT_E_CRITICAL
            || status == CERT_E_PURPOSE
            || status == CERT_E_ISSUERCHAINING
            || status == CERT_E_MALFORMED
            || status == CERT_E_UNTRUSTEDROOT
            || status == CERT_E_REVOKED
            || status == CERT_E_UNTRUSTEDTESTROOT
            || status == CERT_E_CN_NO_MATCH
            || status == CERT_E_WRONG_USAGE
            || status == CRYPT_E_REVOKED
            || status == CRYPT_E_BAD_MSG
            || status == CRYPT_E_SECURITY_SETTINGS
            || status == NTE_BAD_SIGNATURE) {
        return AuthenticodeVerificationStatus::Untrusted;
    }

    if (status == TRUST_E_SUBJECT_FORM_UNKNOWN
            || status == TRUST_E_PROVIDER_UNKNOWN
            || status == TRUST_E_ACTION_UNKNOWN
            || status == TRUST_E_SYSTEM_ERROR
            || status == CRYPT_E_FILE_ERROR
            || status == CRYPT_E_NO_REVOCATION_DLL
            || status == CRYPT_E_NO_REVOCATION_CHECK
            || status == CRYPT_E_REVOCATION_OFFLINE
            || status == CERT_E_REVOCATION_FAILURE
            || status == CERT_E_CHAINING
            || status == E_NOTIMPL
            || status == E_OUTOFMEMORY
            || statusFromWin32(status, ERROR_FILE_NOT_FOUND)
            || statusFromWin32(status, ERROR_PATH_NOT_FOUND)
            || statusFromWin32(status, ERROR_ACCESS_DENIED)
            || statusFromWin32(status, ERROR_SHARING_VIOLATION)
            || statusFromWin32(status, ERROR_LOCK_VIOLATION)
            || statusFromWin32(status, ERROR_NOT_SUPPORTED)
            || statusFromWin32(status, ERROR_NOT_ENOUGH_MEMORY)) {
        return AuthenticodeVerificationStatus::Unavailable;
    }

    return AuthenticodeVerificationStatus::Unavailable;
#else
    Q_UNUSED(nativeStatus);
    return AuthenticodeVerificationStatus::Unavailable;
#endif
}

AuthenticodeVerificationResult AuthenticodeVerifier::verify(
        const ExecutableFileGuard &guard) noexcept
{
    try {
#ifdef Q_OS_WIN
        return verifyWindowsFile(guard);
#else
        AuthenticodeVerificationResult result;
        result.technicalDetail = guard.technicalDetail().isEmpty()
                ? QStringLiteral("当前平台不支持 Authenticode 验证")
                : guard.technicalDetail();
        return result;
#endif
    } catch (const std::exception &error) {
        AuthenticodeVerificationResult result;
        result.technicalDetail = QStringLiteral("验证 Authenticode 时发生异常：%1")
                                         .arg(QString::fromUtf8(error.what()));
        return result;
    } catch (...) {
        AuthenticodeVerificationResult result;
        result.technicalDetail = QStringLiteral("验证 Authenticode 时发生未知异常");
        return result;
    }
}

AuthenticodeVerificationResult AuthenticodeVerifier::verify(
        const QString &path) noexcept
{
    const ExecutableFileGuard guard = ExecutableFileGuard::open(path);
    return verify(guard);
}

} // namespace wam::platform::windows
