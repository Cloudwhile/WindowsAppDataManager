#include "AppResolver.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace wam::core {
namespace {

struct KnownApplication {
    const char *scope;
    const char *relativePath;
    const char *id;
    const char *name;
    const char *publisher;
    const char *category;
    const char *executablePath;
    const char *installPath;
};

const QVector<KnownApplication> &knownApplications()
{
    static const QVector<KnownApplication> applications {
        {"local", "Google/Chrome/User Data", "google-chrome", "Google Chrome",
         "Google LLC", "浏览器", "C:/Program Files/Google/Chrome/Application/chrome.exe",
         "C:/Program Files/Google/Chrome/Application"},
        {"roaming", "discord", "discord", "Discord", "Discord Inc.", "通讯",
         "%LOCALAPPDATA%/Discord/Update.exe", "%LOCALAPPDATA%/Discord"},
        {"roaming", "Code", "visual-studio-code", "Visual Studio Code",
         "Microsoft Corporation", "开发工具",
         "%LOCALAPPDATA%/Programs/Microsoft VS Code/Code.exe",
         "%LOCALAPPDATA%/Programs/Microsoft VS Code"},
        {"local", "JetBrains", "jetbrains", "JetBrains IDE", "JetBrains s.r.o.",
         "开发工具", "C:/Program Files/JetBrains/JetBrains Toolbox/jetbrains-toolbox.exe",
         "C:/Program Files/JetBrains"},
        {"roaming", "JetBrains", "jetbrains", "JetBrains IDE", "JetBrains s.r.o.",
         "开发工具", "C:/Program Files/JetBrains/JetBrains Toolbox/jetbrains-toolbox.exe",
         "C:/Program Files/JetBrains"},
        {"local", "Temp", "windows-temp", "Windows 临时数据", "Microsoft Windows",
         "系统", "", "C:/Windows"},
        {"local", "npm-cache", "npm-cache", "npm 缓存", "OpenJS Foundation",
         "开发工具", "C:/Program Files/nodejs/npm.cmd", "C:/Program Files/nodejs"},
        {"roaming", "npm-cache", "npm-cache", "npm 缓存", "OpenJS Foundation",
         "开发工具", "C:/Program Files/nodejs/npm.cmd", "C:/Program Files/nodejs"}
    };
    return applications;
}

QString rootScope(const QString &root)
{
    const QString name = QFileInfo(root).fileName().toLower();
    if (name == QStringLiteral("local"))
        return QStringLiteral("local");
    if (name == QStringLiteral("roaming"))
        return QStringLiteral("roaming");
    if (name == QStringLiteral("locallow"))
        return QStringLiteral("locallow");
    return QStringLiteral("custom");
}

QString expandEnvironmentPath(QString value)
{
    value.replace(QStringLiteral("%LOCALAPPDATA%"), qEnvironmentVariable("LOCALAPPDATA"),
                  Qt::CaseInsensitive);
    value.replace(QStringLiteral("%APPDATA%"), qEnvironmentVariable("APPDATA"),
                  Qt::CaseInsensitive);
    return QDir::cleanPath(value);
}

QString targetId(const QString &scope, const QString &path)
{
    const QString normalized = QDir::fromNativeSeparators(
            QDir::cleanPath(QFileInfo(path).absoluteFilePath())).toCaseFolded();
    const QByteArray digest = QCryptographicHash::hash(
            normalized.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    return QStringLiteral("unknown-%1-%2").arg(scope, QString::fromLatin1(digest));
}

ApplicationInfo knownApplicationInfo(const KnownApplication &known, const QString &path)
{
    ApplicationInfo application;
    application.id = QString::fromLatin1(known.id);
    application.name = QString::fromUtf8(known.name);
    application.publisher = QString::fromUtf8(known.publisher);
    application.category = QString::fromUtf8(known.category);
    application.location = QDir::toNativeSeparators(path);
    application.executablePath = QDir::toNativeSeparators(
            expandEnvironmentPath(QString::fromUtf8(known.executablePath)));
    application.installPath = QDir::toNativeSeparators(
            expandEnvironmentPath(QString::fromUtf8(known.installPath)));

    const bool executableKnown = !application.executablePath.isEmpty();
    const bool executableExists = executableKnown && QFileInfo::exists(application.executablePath);
    application.installState = !executableKnown || executableExists
            ? InstallState::Installed : InstallState::Unknown;
    application.confidence = executableExists || !executableKnown ? 96 : 82;
    application.risk = RiskLevel::Caution;

    application.evidence.append({EvidenceSource::Folder, EvidenceStatus::Matched,
                                 QStringLiteral("已匹配内置应用目录规则")});
    application.evidence.append({EvidenceSource::Rule, EvidenceStatus::Matched,
                                 QStringLiteral("应用标识与目录范围一致")});
    if (executableKnown) {
        application.evidence.append({EvidenceSource::Executable,
                                     executableExists ? EvidenceStatus::Matched
                                                      : EvidenceStatus::Unavailable,
                                     executableExists ? QStringLiteral("可执行文件存在")
                                                      : QStringLiteral("未找到预期可执行文件")});
    }
    return application;
}

ApplicationInfo unknownApplicationInfo(const QString &scope, const QFileInfo &directory)
{
    ApplicationInfo application;
    application.id = targetId(scope, directory.absoluteFilePath());
    application.name = directory.fileName();
    application.publisher = QStringLiteral("未知");
    application.category = QStringLiteral("未识别");
    application.location = QDir::toNativeSeparators(directory.absoluteFilePath());
    application.installState = InstallState::Unknown;
    application.confidence = 20;
    application.risk = RiskLevel::Unknown;
    application.summary = QStringLiteral("尚未获得足够的应用归属证据，所有数据保持 Unknown。");
    application.evidence.append({EvidenceSource::Folder, EvidenceStatus::Partial,
                                 QStringLiteral("仅获得目录名称证据")});
    application.evidence.append({EvidenceSource::Registry, EvidenceStatus::Unavailable,
                                 QStringLiteral("尚未匹配注册表安装项")});
    return application;
}

} // namespace

QVector<ScanTarget> AppResolver::discoverTargets(const QStringList &roots) const
{
    QVector<ScanTarget> targets;
    for (const QString &root : roots) {
        const QString scope = rootScope(root);
        const QDir rootDirectory(root);
        if (!rootDirectory.exists())
            continue;

        QSet<QString> knownPaths;

        for (const KnownApplication &known : knownApplications()) {
            if (scope != QString::fromLatin1(known.scope))
                continue;

            const QString path = QDir::cleanPath(rootDirectory.filePath(
                    QString::fromUtf8(known.relativePath)));
            if (!QFileInfo(path).isDir())
                continue;

            targets.append({knownApplicationInfo(known, path), path, {}});
            knownPaths.insert(QDir::fromNativeSeparators(path).toCaseFolded());
        }

        const QFileInfoList directories = rootDirectory.entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &directory : directories) {
            const QString path = QDir::cleanPath(directory.absoluteFilePath());
            const QString normalizedPath = QDir::fromNativeSeparators(path).toCaseFolded();
            if (knownPaths.contains(normalizedPath))
                continue;

            QStringList exclusions;
            const QString descendantPrefix = normalizedPath + QLatin1Char('/');
            for (const QString &knownPath : std::as_const(knownPaths)) {
                if (knownPath.startsWith(descendantPrefix))
                    exclusions.append(QDir::toNativeSeparators(knownPath));
            }
            targets.append({unknownApplicationInfo(scope, directory), path, exclusions});
        }
    }

    return targets;
}

} // namespace wam::core
