#include "AppDataPaths.h"

#include <QDir>
#include <QFileInfo>

namespace wam::platform::windows {

QStringList AppDataPaths::roots()
{
    const QString overrideRoot = qEnvironmentVariable("WAM_SCAN_ROOT").trimmed();
    if (!overrideRoot.isEmpty())
        return {QDir::cleanPath(overrideRoot)};

    QStringList result;
    const auto appendExisting = [&result](const QString &path) {
        const QString cleanPath = QDir::cleanPath(path);
        if (!cleanPath.isEmpty() && QFileInfo(cleanPath).isDir() && !result.contains(cleanPath))
            result.append(cleanPath);
    };

    appendExisting(qEnvironmentVariable("LOCALAPPDATA"));
    appendExisting(qEnvironmentVariable("APPDATA"));

    const QString userProfile = qEnvironmentVariable("USERPROFILE");
    if (!userProfile.isEmpty())
        appendExisting(QDir(userProfile).filePath(QStringLiteral("AppData/LocalLow")));

    return result;
}

} // namespace wam::platform::windows
