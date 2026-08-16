#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QVector>

namespace wam {

enum class DataCategory {
    Unknown,
    Cache,
    Log,
    Temp,
    CrashDump,
    Config,
    Database,
    Session,
    Cookie,
    Credential,
    UserData,
    Workspace,
    SaveGame,
    DownloadedResource,
    Extension
};

enum class RiskLevel {
    Safe,
    Low,
    Caution,
    High,
    Protected,
    Unknown
};

enum class RebuildableState {
    Yes,
    No,
    Unknown
};

enum class InstallState {
    Installed,
    PotentialOrphan,
    Unknown
};

enum class EvidenceSource {
    Registry,
    Appx,
    Executable,
    Publisher,
    Folder,
    Rule,
    RunningProcess
};

enum class EvidenceStatus {
    Matched,
    Partial,
    Unavailable,
    Conflict,
    NotFound,
    Incomplete,
    Ambiguous
};

struct EvidenceInfo {
    EvidenceSource source = EvidenceSource::Folder;
    EvidenceStatus status = EvidenceStatus::Unavailable;
    QString detail;
};

struct DataGroupInfo {
    QString id;
    DataCategory category = DataCategory::Unknown;
    quint64 size = 0;
    quint64 fileCount = 0;
    RiskLevel risk = RiskLevel::Unknown;
    RebuildableState rebuildable = RebuildableState::Unknown;
    QString impact;
    QString path;
    QString ruleSource;
};

struct ApplicationInfo {
    QString id;
    QString name;
    QString publisher;
    QString category;
    QString location;
    QString executablePath;
    QString installPath;

    quint64 totalSize = 0;
    quint64 fileCount = 0;
    quint64 reclaimableSize = 0;
    quint64 protectedSize = 0;
    quint64 unknownSize = 0;

    QDateTime lastModified;
    InstallState installState = InstallState::Unknown;
    int confidence = 0;
    RiskLevel risk = RiskLevel::Unknown;
    QString summary;

    QVector<DataGroupInfo> dataGroups;
    QVector<EvidenceInfo> evidence;
};

} // namespace wam

Q_DECLARE_METATYPE(wam::ApplicationInfo)
Q_DECLARE_METATYPE(QVector<wam::ApplicationInfo>)
