#include "ExecutableMetadataReader.h"

#include <QByteArray>
#include <QSet>
#include <QVector>

#include <array>
#include <exception>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <winver.h>
#endif

namespace wam::platform::windows {
namespace {

#ifdef Q_OS_WIN
constexpr DWORD maximumVersionResourceBytes = 16U * 1024U * 1024U;

struct LanguageAndCodePage {
    WORD language = 0;
    WORD codePage = 0;
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
    return QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(length)).trimmed();
}

bool missingVersionResourceError(DWORD status)
{
    return status == ERROR_RESOURCE_TYPE_NOT_FOUND
            || status == ERROR_RESOURCE_NAME_NOT_FOUND
            || status == ERROR_RESOURCE_DATA_NOT_FOUND
            || status == ERROR_BAD_EXE_FORMAT;
}

QString versionString(const QByteArray &resource,
                      const QVector<LanguageAndCodePage> &translations,
                      const QString &field)
{
    QSet<quint32> visited;
    QVector<LanguageAndCodePage> candidates = translations;
    candidates.append({GetUserDefaultLangID(), 1200});
    candidates.append({0x0409, 1200});
    candidates.append({0x0409, 1252});
    candidates.append({0x0000, 1200});

    for (const LanguageAndCodePage candidate : candidates) {
        const quint32 key = (static_cast<quint32>(candidate.language) << 16U)
                | candidate.codePage;
        if (visited.contains(key))
            continue;
        visited.insert(key);

        const QString block = QStringLiteral("\\StringFileInfo\\%1%2\\%3")
                .arg(candidate.language, 4, 16, QLatin1Char('0'))
                .arg(candidate.codePage, 4, 16, QLatin1Char('0'))
                .arg(field);
        void *value = nullptr;
        UINT characterCount = 0;
        if (!VerQueryValueW(
                    const_cast<char *>(resource.constData()),
                    reinterpret_cast<const wchar_t *>(block.utf16()),
                    &value,
                    &characterCount)
                || value == nullptr || characterCount == 0) {
            continue;
        }

        QString result = QString::fromWCharArray(
                static_cast<const wchar_t *>(value),
                static_cast<qsizetype>(characterCount));
        while (result.endsWith(QChar::Null))
            result.chop(1);
        result = result.trimmed();
        if (!result.isEmpty())
            return result;
    }
    return {};
}

bool pathStillReferencesGuardedFile(const ExecutableFileGuard &guard,
                                    QString *technicalDetail)
{
    const ExecutableFileGuard reopened = ExecutableFileGuard::open(
            guard.finalPath());
    if (!reopened.isOpen()) {
        if (technicalDetail) {
            *technicalDetail = reopened.technicalDetail().isEmpty()
                    ? QStringLiteral("无法重新确认版本资源读取期间的文件身份")
                    : reopened.technicalDetail();
        }
        return false;
    }
    if (reopened.identity() != guard.identity()) {
        if (technicalDetail)
            *technicalDetail = QStringLiteral("版本资源读取期间路径指向了另一文件");
        return false;
    }
    return true;
}

ExecutableMetadataResult readWindowsMetadata(const ExecutableFileGuard &guard)
{
    ExecutableMetadataResult result;
    result.path = guard.requestedPath();
    if (guard.openState() == ExecutableFileGuardState::Missing) {
        result.fileState = ExecutableFileState::Missing;
        result.versionInfoState = VersionInfoState::Missing;
        return result;
    }
    if (!guard.isOpen()) {
        result.fileState = ExecutableFileState::Unavailable;
        result.versionInfoState = VersionInfoState::Unavailable;
        result.issues.append(
                guard.technicalDetail().isEmpty()
                        ? QStringLiteral("无法建立稳定的可执行文件句柄")
                        : guard.technicalDetail());
        return result;
    }

    result.fileState = ExecutableFileState::Present;
    result.fileIdentity = guard.identity();
    result.identityStable = true;
    const QString &nativePath = guard.finalPath();
    QString identityDetail;
    if (!pathStillReferencesGuardedFile(guard, &identityDetail)) {
        result.versionInfoState = VersionInfoState::Unavailable;
        result.identityStable = false;
        result.issues.append(identityDetail);
        return result;
    }

    DWORD ignoredHandle = 0;
    SetLastError(ERROR_SUCCESS);
    const DWORD resourceSize = GetFileVersionInfoSizeW(
            reinterpret_cast<const wchar_t *>(nativePath.utf16()), &ignoredHandle);
    if (resourceSize == 0) {
        const DWORD status = GetLastError();
        if (!pathStillReferencesGuardedFile(guard, &identityDetail)) {
            result.versionInfoState = VersionInfoState::Unavailable;
            result.identityStable = false;
            result.issues.append(identityDetail);
            return result;
        }
        if (status == ERROR_SUCCESS || missingVersionResourceError(status)) {
            result.versionInfoState = VersionInfoState::Missing;
        } else {
            result.versionInfoState = VersionInfoState::Unavailable;
            result.issues.append(QStringLiteral("无法读取版本资源大小（Win32 %1）：%2")
                                         .arg(status)
                                         .arg(nativeErrorText(status)));
        }
        return result;
    }
    if (resourceSize > maximumVersionResourceBytes) {
        result.versionInfoState = VersionInfoState::Unavailable;
        result.issues.append(QStringLiteral("版本资源超过安全读取上限"));
        return result;
    }

    QByteArray resource(static_cast<qsizetype>(resourceSize), Qt::Uninitialized);
    if (!GetFileVersionInfoW(
            reinterpret_cast<const wchar_t *>(nativePath.utf16()),
            0,
                resourceSize,
                resource.data())) {
        const DWORD status = GetLastError();
        if (!pathStillReferencesGuardedFile(guard, &identityDetail)) {
            result.versionInfoState = VersionInfoState::Unavailable;
            result.identityStable = false;
            result.issues.append(identityDetail);
            return result;
        }
        result.versionInfoState = VersionInfoState::Unavailable;
        result.issues.append(QStringLiteral("无法读取版本资源（Win32 %1）：%2")
                                     .arg(status)
                                     .arg(nativeErrorText(status)));
        return result;
    }
    if (!pathStillReferencesGuardedFile(guard, &identityDetail)) {
        result.versionInfoState = VersionInfoState::Unavailable;
        result.identityStable = false;
        result.issues.append(identityDetail);
        return result;
    }

    QVector<LanguageAndCodePage> translations;
    void *translationData = nullptr;
    UINT translationBytes = 0;
    if (VerQueryValueW(resource.data(), L"\\VarFileInfo\\Translation",
                       &translationData, &translationBytes)
            && translationData != nullptr
            && translationBytes >= sizeof(LanguageAndCodePage)) {
        const auto *values = static_cast<const LanguageAndCodePage *>(translationData);
        const qsizetype count = static_cast<qsizetype>(
                translationBytes / sizeof(LanguageAndCodePage));
        translations.reserve(count);
        for (qsizetype index = 0; index < count; ++index)
            translations.append(values[index]);
    }

    result.versionInfoState = VersionInfoState::Available;
    result.productName = versionString(resource, translations, QStringLiteral("ProductName"));
    result.companyName = versionString(resource, translations, QStringLiteral("CompanyName"));
    result.fileDescription = versionString(
            resource, translations, QStringLiteral("FileDescription"));
    result.originalFilename = versionString(
            resource, translations, QStringLiteral("OriginalFilename"));
    return result;
}
#endif

} // namespace

ExecutableMetadataResult ExecutableMetadataReader::read(
        const ExecutableFileGuard &guard) noexcept
{
    try {
#ifdef Q_OS_WIN
        return readWindowsMetadata(guard);
#else
        ExecutableMetadataResult result;
        result.path = guard.requestedPath();
        result.issues.append(
                guard.technicalDetail().isEmpty()
                        ? QStringLiteral("当前平台不支持 Windows 可执行文件元数据读取")
                        : guard.technicalDetail());
        return result;
#endif
    } catch (const std::exception &error) {
        ExecutableMetadataResult result;
        result.path = guard.requestedPath();
        result.issues.append(QStringLiteral("读取可执行文件元数据时发生异常：%1")
                                     .arg(QString::fromUtf8(error.what())));
        return result;
    } catch (...) {
        ExecutableMetadataResult result;
        result.path = guard.requestedPath();
        result.issues.append(QStringLiteral("读取可执行文件元数据时发生未知异常"));
        return result;
    }
}

ExecutableMetadataResult ExecutableMetadataReader::read(const QString &path) noexcept
{
    const ExecutableFileGuard guard = ExecutableFileGuard::open(path);
    return read(guard);
}

} // namespace wam::platform::windows
