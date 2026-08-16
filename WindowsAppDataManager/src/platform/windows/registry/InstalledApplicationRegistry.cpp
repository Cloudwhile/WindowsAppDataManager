#include "InstalledApplicationRegistry.h"

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
constexpr wchar_t uninstallRegistryPath[] =
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
constexpr DWORD maximumRegistryValueBytes = 1024U * 1024U;
constexpr DWORD maximumRegistryKeyNameCharacters = 32U * 1024U;
constexpr REGSAM rootReadAccess = KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE;

class RegistryKey final {
public:
    RegistryKey() = default;

    explicit RegistryKey(HKEY key) noexcept
        : m_key(key)
    {
    }

    ~RegistryKey()
    {
        reset();
    }

    RegistryKey(const RegistryKey &) = delete;
    RegistryKey &operator=(const RegistryKey &) = delete;

    RegistryKey(RegistryKey &&other) noexcept
        : m_key(std::exchange(other.m_key, nullptr))
    {
    }

    RegistryKey &operator=(RegistryKey &&other) noexcept
    {
        if (this != &other) {
            reset();
            m_key = std::exchange(other.m_key, nullptr);
        }
        return *this;
    }

    [[nodiscard]] HKEY get() const noexcept
    {
        return m_key;
    }

private:
    void reset() noexcept
    {
        if (m_key != nullptr) {
            RegCloseKey(m_key);
            m_key = nullptr;
        }
    }

    HKEY m_key = nullptr;
};

struct RegistryLocation {
    HKEY root = nullptr;
    RegistryHive hive = RegistryHive::CurrentUser;
    RegistryView view = RegistryView::Registry64;
};

template<typename T>
struct RegistryValueResult {
    std::optional<T> value;
    LSTATUS status = ERROR_SUCCESS;
};

[[nodiscard]] REGSAM viewAccessMask(RegistryView view) noexcept
{
    return view == RegistryView::Registry32 ? KEY_WOW64_32KEY : KEY_WOW64_64KEY;
}

[[nodiscard]] QString hiveName(RegistryHive hive)
{
    return hive == RegistryHive::CurrentUser
            ? QStringLiteral("HKEY_CURRENT_USER")
            : QStringLiteral("HKEY_LOCAL_MACHINE");
}

[[nodiscard]] QString registryRootPath(RegistryHive hive)
{
    return QStringLiteral("%1\\%2")
            .arg(hiveName(hive), QString::fromWCharArray(uninstallRegistryPath));
}

[[nodiscard]] RegistryReadError classifyError(
        LSTATUS status, RegistryReadError fallback) noexcept
{
    switch (status) {
    case ERROR_ACCESS_DENIED:
        return RegistryReadError::AccessDenied;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return RegistryReadError::KeyUnavailable;
    case ERROR_BAD_LENGTH:
    case ERROR_INVALID_DATA:
    case ERROR_INVALID_PARAMETER:
    case ERROR_MORE_DATA:
        return RegistryReadError::InvalidValue;
    default:
        return fallback;
    }
}

[[nodiscard]] QString nativeErrorText(LSTATUS status)
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
    if (length == 0)
        return QStringLiteral("Win32 error %1").arg(status);

    return QString::fromWCharArray(buffer.data(), static_cast<qsizetype>(length)).trimmed();
}

void appendIssue(RegistryInstallQueryResult &result,
                 RegistryHive hive,
                 RegistryView view,
                 const QString &keyPath,
                 LSTATUS status,
                 RegistryReadError fallback,
                 const QString &operation)
{
    RegistryReadIssue issue;
    issue.error = classifyError(status, fallback);
    issue.hive = hive;
    issue.view = view;
    issue.keyPath = keyPath;
    issue.nativeError = static_cast<quint32>(status);
    issue.technicalDetail = QStringLiteral("%1: %2")
                                    .arg(operation, nativeErrorText(status));
    result.issues.append(std::move(issue));
    result.complete = false;
}

[[nodiscard]] RegistryValueResult<QString> readStringValue(
        HKEY key, const wchar_t *valueName)
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        DWORD type = REG_NONE;
        DWORD byteCount = 0;
        LSTATUS status = RegQueryValueExW(
                key, valueName, nullptr, &type, nullptr, &byteCount);
        if (status == ERROR_FILE_NOT_FOUND)
            return {};
        if (status != ERROR_SUCCESS)
            return {{}, status};
        if (type != REG_SZ && type != REG_EXPAND_SZ)
            return {{}, ERROR_INVALID_DATA};
        if (byteCount > maximumRegistryValueBytes
                || byteCount % sizeof(wchar_t) != 0) {
            return {{}, ERROR_BAD_LENGTH};
        }
        if (byteCount == 0)
            return {QString(), ERROR_SUCCESS};

        const auto characterCapacity = static_cast<std::size_t>(
                byteCount / sizeof(wchar_t)) + 1U;
        std::vector<wchar_t> buffer(characterCapacity, L'\0');
        DWORD returnedType = REG_NONE;
        DWORD returnedBytes = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
        status = RegQueryValueExW(
                key,
                valueName,
                nullptr,
                &returnedType,
                reinterpret_cast<BYTE *>(buffer.data()),
                &returnedBytes);
        if (status == ERROR_MORE_DATA)
            continue;
        if (status == ERROR_FILE_NOT_FOUND)
            return {};
        if (status != ERROR_SUCCESS)
            return {{}, status};
        if (returnedType != REG_SZ && returnedType != REG_EXPAND_SZ)
            return {{}, ERROR_INVALID_DATA};
        if (returnedBytes > maximumRegistryValueBytes
                || returnedBytes % sizeof(wchar_t) != 0) {
            return {{}, ERROR_BAD_LENGTH};
        }

        qsizetype characterCount = static_cast<qsizetype>(
                returnedBytes / sizeof(wchar_t));
        while (characterCount > 0 && buffer[static_cast<std::size_t>(characterCount - 1)] == L'\0')
            --characterCount;
        for (qsizetype index = 0; index < characterCount; ++index) {
            if (buffer[static_cast<std::size_t>(index)] == L'\0') {
                characterCount = index;
                break;
            }
        }

        return {QString::fromWCharArray(buffer.data(), characterCount), ERROR_SUCCESS};
    }

    return {{}, ERROR_MORE_DATA};
}

[[nodiscard]] RegistryValueResult<quint32> readDwordValue(
        HKEY key, const wchar_t *valueName)
{
    DWORD type = REG_NONE;
    DWORD value = 0;
    DWORD byteCount = sizeof(value);
    const LSTATUS status = RegQueryValueExW(
            key,
            valueName,
            nullptr,
            &type,
            reinterpret_cast<BYTE *>(&value),
            &byteCount);
    if (status == ERROR_FILE_NOT_FOUND)
        return {};
    if (status != ERROR_SUCCESS)
        return {{}, status};
    if (type != REG_DWORD || byteCount != sizeof(value))
        return {{}, ERROR_INVALID_DATA};
    return {static_cast<quint32>(value), ERROR_SUCCESS};
}

void readStringField(RegistryInstallQueryResult &result,
                     RegistryInstallEntry &entry,
                     HKEY key,
                     const wchar_t *valueName,
                     QString &destination)
{
    const RegistryValueResult<QString> value = readStringValue(key, valueName);
    if (value.status == ERROR_SUCCESS) {
        if (value.value)
            destination = *value.value;
        return;
    }

    const QString valuePath = QStringLiteral("%1\\%2")
                                      .arg(entry.uninstallKeyPath,
                                           QString::fromWCharArray(valueName));
    appendIssue(result,
                entry.hive,
                entry.view,
                valuePath,
                value.status,
                RegistryReadError::EntryUnavailable,
                QStringLiteral("Unable to read registry string value"));
}

[[nodiscard]] std::optional<quint32> readDwordField(
        RegistryInstallQueryResult &result,
        const RegistryInstallEntry &entry,
        HKEY key,
        const wchar_t *valueName)
{
    const RegistryValueResult<quint32> value = readDwordValue(key, valueName);
    if (value.status == ERROR_SUCCESS)
        return value.value;

    const QString valuePath = QStringLiteral("%1\\%2")
                                      .arg(entry.uninstallKeyPath,
                                           QString::fromWCharArray(valueName));
    appendIssue(result,
                entry.hive,
                entry.view,
                valuePath,
                value.status,
                RegistryReadError::EntryUnavailable,
                QStringLiteral("Unable to read registry DWORD value"));
    return std::nullopt;
}

[[nodiscard]] RegistryInstallEntry readEntry(
        RegistryInstallQueryResult &result,
        const RegistryLocation &location,
        const QString &subKeyName,
        const QString &subKeyPath,
        HKEY key)
{
    RegistryInstallEntry entry;
    entry.hive = location.hive;
    entry.view = location.view;
    entry.uninstallKeyName = subKeyName;
    entry.uninstallKeyPath = subKeyPath;

    readStringField(result, entry, key, L"DisplayName", entry.displayName);
    readStringField(result, entry, key, L"Publisher", entry.publisher);
    readStringField(result, entry, key, L"DisplayVersion", entry.displayVersion);
    readStringField(result, entry, key, L"InstallLocation", entry.installLocation);
    readStringField(result, entry, key, L"DisplayIcon", entry.displayIcon);
    readStringField(result, entry, key, L"UninstallString", entry.uninstallString);
    readStringField(result, entry, key, L"QuietUninstallString", entry.quietUninstallString);
    readStringField(result, entry, key, L"ModifyPath", entry.modifyPath);
    readStringField(result, entry, key, L"InstallSource", entry.installSource);
    readStringField(result, entry, key, L"InstallDate", entry.installDate);

    const std::optional<quint32> estimatedSize = readDwordField(
            result, entry, key, L"EstimatedSize");
    if (estimatedSize)
        entry.estimatedSizeKiB = static_cast<quint64>(*estimatedSize);

    const std::optional<quint32> windowsInstaller = readDwordField(
            result, entry, key, L"WindowsInstaller");
    if (windowsInstaller)
        entry.windowsInstaller = *windowsInstaller != 0;

    const std::optional<quint32> systemComponent = readDwordField(
            result, entry, key, L"SystemComponent");
    if (systemComponent)
        entry.systemComponent = *systemComponent != 0;

    return entry;
}

void enumerateLocation(RegistryInstallQueryResult &result,
                       const RegistryLocation &location)
{
    const QString rootPath = registryRootPath(location.hive);
    HKEY rawRoot = nullptr;
    const LSTATUS openStatus = RegOpenKeyExW(
            location.root,
            uninstallRegistryPath,
            0,
            rootReadAccess | viewAccessMask(location.view),
            &rawRoot);
    if (openStatus == ERROR_FILE_NOT_FOUND || openStatus == ERROR_PATH_NOT_FOUND)
        return;
    if (openStatus != ERROR_SUCCESS) {
        appendIssue(result,
                    location.hive,
                    location.view,
                    rootPath,
                    openStatus,
                    RegistryReadError::KeyUnavailable,
                    QStringLiteral("Unable to open uninstall registry key"));
        return;
    }
    RegistryKey root(rawRoot);

    DWORD maximumNameLength = 0;
    const LSTATUS infoStatus = RegQueryInfoKeyW(
            root.get(),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            &maximumNameLength,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr);
    if (infoStatus != ERROR_SUCCESS) {
        appendIssue(result,
                    location.hive,
                    location.view,
                    rootPath,
                    infoStatus,
                    RegistryReadError::EnumerationFailed,
                    QStringLiteral("Unable to inspect uninstall registry key"));
        return;
    }
    if (maximumNameLength > maximumRegistryKeyNameCharacters) {
        appendIssue(result,
                    location.hive,
                    location.view,
                    rootPath,
                    ERROR_BAD_LENGTH,
                    RegistryReadError::InvalidValue,
                    QStringLiteral("Registry subkey name exceeds the safety limit"));
        return;
    }

    std::vector<wchar_t> nameBuffer(
            std::max<std::size_t>(static_cast<std::size_t>(maximumNameLength) + 1U, 256U),
            L'\0');
    for (DWORD index = 0;;) {
        DWORD nameLength = static_cast<DWORD>(nameBuffer.size());
        FILETIME lastWriteTime {};
        const LSTATUS enumStatus = RegEnumKeyExW(
                root.get(),
                index,
                nameBuffer.data(),
                &nameLength,
                nullptr,
                nullptr,
                nullptr,
                &lastWriteTime);
        if (enumStatus == ERROR_NO_MORE_ITEMS)
            break;
        if (enumStatus == ERROR_MORE_DATA) {
            if (nameBuffer.size() >= maximumRegistryKeyNameCharacters + 1U) {
                appendIssue(result,
                            location.hive,
                            location.view,
                            rootPath,
                            enumStatus,
                            RegistryReadError::EnumerationFailed,
                            QStringLiteral("Registry subkey name exceeds the safety limit"));
                break;
            }
            nameBuffer.resize(std::min<std::size_t>(
                    nameBuffer.size() * 2U,
                    maximumRegistryKeyNameCharacters + 1U));
            continue;
        }
        if (enumStatus != ERROR_SUCCESS) {
            appendIssue(result,
                        location.hive,
                        location.view,
                        rootPath,
                        enumStatus,
                        RegistryReadError::EnumerationFailed,
                        QStringLiteral("Unable to enumerate uninstall registry key"));
            break;
        }
        ++index;

        const QString subKeyName = QString::fromWCharArray(
                nameBuffer.data(), static_cast<qsizetype>(nameLength));
        const QString subKeyPath = QStringLiteral("%1\\%2").arg(rootPath, subKeyName);
        HKEY rawEntry = nullptr;
        const LSTATUS entryOpenStatus = RegOpenKeyExW(
                root.get(),
                nameBuffer.data(),
                0,
                KEY_QUERY_VALUE | viewAccessMask(location.view),
                &rawEntry);
        if (entryOpenStatus != ERROR_SUCCESS) {
            appendIssue(result,
                        location.hive,
                        location.view,
                        subKeyPath,
                        entryOpenStatus,
                        RegistryReadError::EntryUnavailable,
                        QStringLiteral("Unable to open uninstall registry entry"));
            continue;
        }
        RegistryKey entryKey(rawEntry);
        result.entries.append(readEntry(
                result, location, subKeyName, subKeyPath, entryKey.get()));
    }
}

[[nodiscard]] bool entryLessThan(const RegistryInstallEntry &left,
                                 const RegistryInstallEntry &right)
{
    const int displayNameOrder = left.displayName.compare(
            right.displayName, Qt::CaseInsensitive);
    if (displayNameOrder != 0)
        return displayNameOrder < 0;
    if (left.hive != right.hive)
        return static_cast<int>(left.hive) < static_cast<int>(right.hive);
    if (left.view != right.view)
        return static_cast<int>(left.view) < static_cast<int>(right.view);
    return left.uninstallKeyPath.compare(
                   right.uninstallKeyPath, Qt::CaseInsensitive) < 0;
}
#endif

} // namespace

RegistryInstallQueryResult InstalledApplicationRegistry::query() noexcept
{
    RegistryInstallQueryResult result;
#ifdef Q_OS_WIN
    result.supported = true;
    result.complete = true;

    const std::array<RegistryLocation, 4> locations {{
        {HKEY_CURRENT_USER, RegistryHive::CurrentUser, RegistryView::Registry64},
        {HKEY_CURRENT_USER, RegistryHive::CurrentUser, RegistryView::Registry32},
        {HKEY_LOCAL_MACHINE, RegistryHive::LocalMachine, RegistryView::Registry64},
        {HKEY_LOCAL_MACHINE, RegistryHive::LocalMachine, RegistryView::Registry32}
    }};

    for (const RegistryLocation &location : locations) {
        try {
            enumerateLocation(result, location);
        } catch (const std::exception &error) {
            appendIssue(result,
                        location.hive,
                        location.view,
                        registryRootPath(location.hive),
                        ERROR_UNHANDLED_EXCEPTION,
                        RegistryReadError::UnexpectedFailure,
                        QStringLiteral("Unexpected registry enumeration failure: %1")
                                .arg(QString::fromUtf8(error.what())));
        } catch (...) {
            appendIssue(result,
                        location.hive,
                        location.view,
                        registryRootPath(location.hive),
                        ERROR_UNHANDLED_EXCEPTION,
                        RegistryReadError::UnexpectedFailure,
                        QStringLiteral("Unexpected registry enumeration failure"));
        }
    }

    try {
        std::sort(result.entries.begin(), result.entries.end(), entryLessThan);
    } catch (const std::exception &error) {
        appendIssue(result,
                    RegistryHive::CurrentUser,
                    RegistryView::Registry64,
                    QStringLiteral("RegistryInstallQueryResult"),
                    ERROR_UNHANDLED_EXCEPTION,
                    RegistryReadError::UnexpectedFailure,
                    QStringLiteral("Unable to sort registry results: %1")
                            .arg(QString::fromUtf8(error.what())));
    } catch (...) {
        appendIssue(result,
                    RegistryHive::CurrentUser,
                    RegistryView::Registry64,
                    QStringLiteral("RegistryInstallQueryResult"),
                    ERROR_UNHANDLED_EXCEPTION,
                    RegistryReadError::UnexpectedFailure,
                    QStringLiteral("Unable to sort registry results"));
    }
#endif
    return result;
}

} // namespace wam::platform::windows
