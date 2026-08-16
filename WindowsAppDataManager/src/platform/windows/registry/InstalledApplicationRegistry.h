#pragma once

#include <QString>
#include <QVector>

#include <optional>

namespace wam::platform::windows {

enum class RegistryHive {
    CurrentUser,
    LocalMachine
};

enum class RegistryView {
    Registry32,
    Registry64
};

enum class RegistryReadError {
    AccessDenied,
    KeyUnavailable,
    EnumerationFailed,
    EntryUnavailable,
    InvalidValue,
    UnexpectedFailure
};

struct RegistryInstallEntry {
    RegistryHive hive = RegistryHive::CurrentUser;
    RegistryView view = RegistryView::Registry64;

    QString uninstallKeyName;
    QString uninstallKeyPath;
    QString displayName;
    QString publisher;
    QString displayVersion;
    QString installLocation;
    QString displayIcon;
    QString uninstallString;
    QString quietUninstallString;
    QString modifyPath;
    QString installSource;
    QString installDate;

    std::optional<quint64> estimatedSizeKiB;
    std::optional<bool> windowsInstaller;
    std::optional<bool> systemComponent;
};

struct RegistryReadIssue {
    RegistryReadError error = RegistryReadError::UnexpectedFailure;
    RegistryHive hive = RegistryHive::CurrentUser;
    RegistryView view = RegistryView::Registry64;
    QString keyPath;
    quint32 nativeError = 0;
    QString technicalDetail;
};

struct RegistryInstallQueryResult {
    QVector<RegistryInstallEntry> entries;
    QVector<RegistryReadIssue> issues;
    bool supported = false;
    bool complete = false;
};

class InstalledApplicationRegistry final {
public:
    [[nodiscard]] static RegistryInstallQueryResult query() noexcept;
};

} // namespace wam::platform::windows
