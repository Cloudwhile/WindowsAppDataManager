#pragma once

#include "ExecutableFileIdentity.h"

#include <QString>
#include <QStringList>

namespace wam::platform::windows {

enum class ExecutableFileState {
    Present,
    Missing,
    Unavailable
};

enum class VersionInfoState {
    Available,
    Missing,
    Unavailable
};

struct ExecutableMetadataResult {
    QString path;
    ExecutableFileState fileState = ExecutableFileState::Unavailable;
    VersionInfoState versionInfoState = VersionInfoState::Unavailable;
    ExecutableFileIdentity fileIdentity;
    bool identityStable = false;
    QString productName;
    QString companyName;
    QString fileDescription;
    QString originalFilename;
    QStringList issues;
};

class ExecutableMetadataReader final {
public:
    [[nodiscard]] static ExecutableMetadataResult read(
            const ExecutableFileGuard &guard) noexcept;
    [[nodiscard]] static ExecutableMetadataResult read(const QString &path) noexcept;
};

} // namespace wam::platform::windows
