#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace wam::platform::windows {

struct AppxPackageInfo {
    QString name;
    QString familyName;
    QString publisherId;
    QString publisher;
    QString displayName;
    QString publisherDisplayName;
    QString installPath;
    bool framework = false;
    bool resourcePackage = false;
};

struct AppxPackageQueryResult {
    QVector<AppxPackageInfo> packages;
    QStringList issues;
    bool available = false;
};

class AppxPackageCatalog final {
public:
    [[nodiscard]] static AppxPackageQueryResult installedForCurrentUser();
};

} // namespace wam::platform::windows
