#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QStringList>
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
    RunningProcess,
    InstallPath
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

    bool operator==(const EvidenceInfo &) const = default;
};

struct OrphanAssessment {
    InstallState state = InstallState::Unknown;
    int confidence = 0;
    QString summary;
    QStringList supportingEvidence;
    QStringList blockingReasons;
    QDateTime assessedAt;
    bool evaluated = false;

    bool operator==(const OrphanAssessment &) const = default;
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

    bool operator==(const DataGroupInfo &) const = default;
};

struct CleanupCandidateInfo {
    QString id;
    QString applicationId;
    QString applicationName;
    QString applicationRoot;
    QString executablePath;
    QString path;
    QString ruleEntryId;
    QString ruleSource;
    DataCategory category = DataCategory::Unknown;
    RiskLevel risk = RiskLevel::Unknown;
    RebuildableState rebuildable = RebuildableState::Unknown;
    QString impact;
    quint64 size = 0;
    quint64 fileCount = 0;
    QString metadataFingerprint;
    QDateTime lastModified;
    quint64 volumeSerialNumber = 0;
    quint64 fileIndex = 0;
    bool identityValid = false;
    bool directory = false;
    bool verifiedRule = false;
    bool exclusiveLocation = false;
    bool scanComplete = false;
    bool containsUnsafeData = false;

    bool operator==(const CleanupCandidateInfo &) const = default;
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
    bool scanComplete = false;
    OrphanAssessment orphanAssessment;
    RiskLevel risk = RiskLevel::Unknown;
    QString summary;

    QVector<DataGroupInfo> dataGroups;
    QVector<EvidenceInfo> evidence;
    QVector<CleanupCandidateInfo> cleanupCandidates;

    bool operator==(const ApplicationInfo &) const = default;
};

} // namespace wam

Q_DECLARE_METATYPE(wam::ApplicationInfo)
Q_DECLARE_METATYPE(wam::CleanupCandidateInfo)
Q_DECLARE_METATYPE(QVector<wam::ApplicationInfo>)
