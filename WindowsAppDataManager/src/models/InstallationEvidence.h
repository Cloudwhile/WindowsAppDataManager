#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace wam {

enum class InstallationEvidenceAvailability {
    Complete,
    Partial,
    Unavailable
};

enum class ExecutablePathState {
    Present,
    Missing,
    Unavailable
};

enum class VersionMetadataState {
    Available,
    Missing,
    Unavailable
};

enum class AuthenticodeState {
    Trusted,
    Unsigned,
    Untrusted,
    Unavailable
};

struct RegistryInstallationRecord {
    QString identity;
    QString displayName;
    QString publisher;
    QString installPath;
};

struct AppxInstallationRecord {
    QString packageName;
    QString publisher;
    QString packageFamilyName;
    QString displayName;
    QString installPath;
};

struct ExecutableEvidenceRecord {
    QString path;
    ExecutablePathState pathState = ExecutablePathState::Unavailable;
    VersionMetadataState metadataState = VersionMetadataState::Unavailable;
    QString productName;
    QString companyName;
    QString fileDescription;
    QString originalFilename;
    AuthenticodeState authenticodeState = AuthenticodeState::Unavailable;
    QString signerPublisher;
};

struct RunningProcessEvidenceRecord {
    quint32 processId = 0;
    QString imageName;
    QString imagePath;
};

template <typename Record>
struct InstallationEvidenceSourceSnapshot {
    InstallationEvidenceAvailability availability =
            InstallationEvidenceAvailability::Unavailable;
    QVector<Record> records;
    QStringList issues;
};

struct InstallationEvidenceSnapshot {
    InstallationEvidenceSourceSnapshot<RegistryInstallationRecord> registry;
    InstallationEvidenceSourceSnapshot<AppxInstallationRecord> appx;
    InstallationEvidenceSourceSnapshot<ExecutableEvidenceRecord> executable;
    InstallationEvidenceSourceSnapshot<RunningProcessEvidenceRecord> runningProcesses;
};

} // namespace wam
