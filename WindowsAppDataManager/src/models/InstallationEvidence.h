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
};

} // namespace wam
