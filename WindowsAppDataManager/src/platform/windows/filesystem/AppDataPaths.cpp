#include "AppDataPaths.h"

#include <QDir>
namespace wam::platform::windows {

QStringList AppDataPaths::roots()
{
    const QString overrideRoot = qEnvironmentVariable("WAM_SCAN_ROOT").trimmed();
    if (!overrideRoot.isEmpty())
        return {QDir::cleanPath(overrideRoot)};

    QStringList result;
    const auto appendConfigured = [&result](const QString &path) {
        const QString cleanPath = QDir::cleanPath(path);
        if (!cleanPath.isEmpty() && !result.contains(cleanPath))
            result.append(cleanPath);
    };

    appendConfigured(qEnvironmentVariable("LOCALAPPDATA"));
    appendConfigured(qEnvironmentVariable("APPDATA"));

    const QString userProfile = qEnvironmentVariable("USERPROFILE");
    if (!userProfile.isEmpty())
        appendConfigured(QDir(userProfile).filePath(QStringLiteral("AppData/LocalLow")));

    return result;
}

} // namespace wam::platform::windows
