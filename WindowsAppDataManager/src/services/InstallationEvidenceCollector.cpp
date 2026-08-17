#include "InstallationEvidenceCollector.h"

#include "../core/rules/RulePathResolver.h"
#include "../platform/windows/appx/AppxPackageCatalog.h"
#include "../platform/windows/filesystem/ExecutableMetadataReader.h"
#include "../platform/windows/registry/InstalledApplicationRegistry.h"
#include "../platform/windows/security/AuthenticodeVerifier.h"

#include <QHash>
#include <QLoggingCategory>
#include <QSet>

#include <algorithm>
#include <utility>

namespace wam::services {
namespace {

Q_LOGGING_CATEGORY(evidenceLog, "wam.evidence")

QString availabilityName(InstallationEvidenceAvailability availability)
{
    switch (availability) {
    case InstallationEvidenceAvailability::Complete:
        return QStringLiteral("完整");
    case InstallationEvidenceAvailability::Partial:
        return QStringLiteral("部分可用");
    case InstallationEvidenceAvailability::Unavailable:
        return QStringLiteral("不可用");
    }
    return QStringLiteral("未知");
}

void logIssues(const QString &source,
               InstallationEvidenceAvailability availability,
               const QStringList &issues)
{
    if (issues.isEmpty() && availability == InstallationEvidenceAvailability::Complete)
        return;

    const QString detail = issues.isEmpty()
            ? QStringLiteral("平台未提供技术详情")
            : issues.join(QStringLiteral(" | "));
    qCWarning(evidenceLog).noquote()
            << QStringLiteral("安装证据采集警告 [%1，状态：%2，问题：%3 条]：%4")
                       .arg(source,
                            availabilityName(availability),
                            QString::number(issues.size()),
                            detail);
}

void collectRegistryEvidence(InstallationEvidenceSnapshot &snapshot)
{
    const platform::windows::RegistryInstallQueryResult registry =
            platform::windows::InstalledApplicationRegistry::query();
    if (!registry.supported) {
        snapshot.registry.availability = InstallationEvidenceAvailability::Unavailable;
    } else if (registry.complete) {
        snapshot.registry.availability = InstallationEvidenceAvailability::Complete;
    } else {
        snapshot.registry.availability = InstallationEvidenceAvailability::Partial;
    }

    snapshot.registry.records.reserve(registry.entries.size());
    for (const platform::windows::RegistryInstallEntry &entry : registry.entries) {
        if (entry.displayName.trimmed().isEmpty())
            continue;
        const QString view = entry.view == platform::windows::RegistryView::Registry32
                ? QStringLiteral("32") : QStringLiteral("64");
        snapshot.registry.records.append({
            QStringLiteral("%1|%2").arg(entry.uninstallKeyPath, view),
            entry.displayName,
            entry.publisher,
            entry.installLocation
        });
    }

    snapshot.registry.issues.reserve(registry.issues.size());
    for (const platform::windows::RegistryReadIssue &issue : registry.issues) {
        const QString hive = issue.hive == platform::windows::RegistryHive::CurrentUser
                ? QStringLiteral("HKCU") : QStringLiteral("HKLM");
        const QString view = issue.view == platform::windows::RegistryView::Registry32
                ? QStringLiteral("32") : QStringLiteral("64");
        snapshot.registry.issues.append(
                QStringLiteral("%1 %2 位 / %3 / Win32 %4：%5")
                        .arg(hive,
                             view,
                             issue.keyPath,
                             QString::number(issue.nativeError),
                             issue.technicalDetail));
    }
}

void collectAppxEvidence(InstallationEvidenceSnapshot &snapshot)
{
    const platform::windows::AppxPackageQueryResult appx =
            platform::windows::AppxPackageCatalog::installedForCurrentUser();
    if (!appx.available) {
        snapshot.appx.availability = InstallationEvidenceAvailability::Unavailable;
    } else if (appx.issues.isEmpty()) {
        snapshot.appx.availability = InstallationEvidenceAvailability::Complete;
    } else {
        snapshot.appx.availability = InstallationEvidenceAvailability::Partial;
    }
    snapshot.appx.issues = appx.issues;
    snapshot.appx.records.reserve(appx.packages.size());
    for (const platform::windows::AppxPackageInfo &package : appx.packages) {
        if (package.resourcePackage || package.name.trimmed().isEmpty())
            continue;
        snapshot.appx.records.append({
            package.name,
            package.publisher,
            package.familyName,
            package.displayName,
            package.installPath
        });
    }
}

ExecutablePathState pathState(platform::windows::ExecutableFileState state)
{
    switch (state) {
    case platform::windows::ExecutableFileState::Present:
        return ExecutablePathState::Present;
    case platform::windows::ExecutableFileState::Missing:
        return ExecutablePathState::Missing;
    case platform::windows::ExecutableFileState::Unavailable:
        return ExecutablePathState::Unavailable;
    }
    return ExecutablePathState::Unavailable;
}

VersionMetadataState metadataState(platform::windows::VersionInfoState state)
{
    switch (state) {
    case platform::windows::VersionInfoState::Available:
        return VersionMetadataState::Available;
    case platform::windows::VersionInfoState::Missing:
        return VersionMetadataState::Missing;
    case platform::windows::VersionInfoState::Unavailable:
        return VersionMetadataState::Unavailable;
    }
    return VersionMetadataState::Unavailable;
}

AuthenticodeState authenticodeState(
        platform::windows::AuthenticodeVerificationStatus status)
{
    switch (status) {
    case platform::windows::AuthenticodeVerificationStatus::Trusted:
        return AuthenticodeState::Trusted;
    case platform::windows::AuthenticodeVerificationStatus::Unsigned:
        return AuthenticodeState::Unsigned;
    case platform::windows::AuthenticodeVerificationStatus::Untrusted:
        return AuthenticodeState::Untrusted;
    case platform::windows::AuthenticodeVerificationStatus::Unavailable:
        return AuthenticodeState::Unavailable;
    }
    return AuthenticodeState::Unavailable;
}

QString fileIdentityKey(
        const platform::windows::ExecutableFileIdentity &identity)
{
    if (!identity.valid)
        return {};
    return QStringLiteral("%1:%2:%3:%4")
            .arg(identity.volumeSerialNumber, 0, 16)
            .arg(identity.fileIndex, 0, 16)
            .arg(identity.fileSize, 0, 16)
            .arg(identity.lastWriteTime, 0, 16);
}

void discardAmbiguousPhysicalFile(ExecutableEvidenceRecord &record)
{
    record.pathState = ExecutablePathState::Unavailable;
    record.metadataState = VersionMetadataState::Unavailable;
    record.productName.clear();
    record.companyName.clear();
    record.fileDescription.clear();
    record.originalFilename.clear();
    record.authenticodeState = AuthenticodeState::Unavailable;
    record.signerPublisher.clear();
}

void collectExecutableEvidence(const core::rules::RuleCatalog &catalog,
                               InstallationEvidenceSnapshot &snapshot)
{
    snapshot.executable.availability = InstallationEvidenceAvailability::Complete;
    QSet<QString> collectedPaths;
    QHash<QString, qsizetype> physicalFileRecords;
    bool incomplete = false;

    for (const ApplicationRule &rule : catalog.applications()) {
        const core::rules::RulePathResolution resolution =
                core::rules::resolveRulePath(rule.executablePath);
        if (!resolution.isResolved()) {
            const QString claimKey = core::rules::normalizedRulePathClaim(
                    rule.executablePath);
            if (collectedPaths.contains(claimKey))
                continue;
            collectedPaths.insert(claimKey);

            ExecutableEvidenceRecord record;
            record.path = rule.executablePath;
            record.pathState = ExecutablePathState::Unavailable;
            record.metadataState = VersionMetadataState::Unavailable;
            record.authenticodeState = AuthenticodeState::Unavailable;
            snapshot.executable.records.append(std::move(record));
            snapshot.executable.issues.append(
                    QStringLiteral("%1：%2")
                            .arg(rule.executablePath,
                                 resolution.detail.isEmpty()
                                         ? QStringLiteral("规则路径当前无法解析")
                                         : resolution.detail));
            incomplete = true;
            continue;
        }

        const QString requestedPath = resolution.path;
        const QString key = core::rules::normalizedPathKey(requestedPath);
        if (collectedPaths.contains(key))
            continue;
        collectedPaths.insert(key);

        const platform::windows::ExecutableFileGuard fileGuard =
                platform::windows::ExecutableFileGuard::open(requestedPath);
        const QString physicalFileKey = fileGuard.isOpen()
                ? fileIdentityKey(fileGuard.identity()) : QString {};
        const auto existingPhysicalFile =
                physicalFileRecords.constFind(physicalFileKey);
        if (!physicalFileKey.isEmpty()
                && existingPhysicalFile != physicalFileRecords.cend()) {
            ExecutableEvidenceRecord &previous =
                    snapshot.executable.records[*existingPhysicalFile];
            const QString previousPath = previous.path;
            discardAmbiguousPhysicalFile(previous);

            ExecutableEvidenceRecord duplicate;
            duplicate.path = requestedPath;
            discardAmbiguousPhysicalFile(duplicate);
            snapshot.executable.records.append(std::move(duplicate));
            snapshot.executable.issues.append(
                    QStringLiteral("%1 与 %2 指向同一物理文件，已丢弃两条应用归属证据")
                            .arg(previousPath, requestedPath));
            incomplete = true;
            continue;
        }
        if (!physicalFileKey.isEmpty()) {
            physicalFileRecords.insert(
                    physicalFileKey, snapshot.executable.records.size());
        }

        const platform::windows::ExecutableMetadataResult metadata =
                platform::windows::ExecutableMetadataReader::read(fileGuard);
        ExecutableEvidenceRecord record;
        record.path = requestedPath;
        record.pathState = pathState(metadata.fileState);
        record.metadataState = metadataState(metadata.versionInfoState);
        record.productName = metadata.productName;
        record.companyName = metadata.companyName;
        record.fileDescription = metadata.fileDescription;
        record.originalFilename = metadata.originalFilename;

        for (const QString &issue : metadata.issues) {
            snapshot.executable.issues.append(
                    QStringLiteral("%1：%2").arg(record.path, issue));
        }

        if (record.pathState == ExecutablePathState::Present) {
            const platform::windows::AuthenticodeVerificationResult signature =
                    platform::windows::AuthenticodeVerifier::verify(fileGuard);
            const bool sameFile = metadata.identityStable
                    && signature.identityStable
                    && metadata.fileIdentity == signature.fileIdentity;
            if (!sameFile) {
                record.pathState = ExecutablePathState::Unavailable;
                record.metadataState = VersionMetadataState::Unavailable;
                record.productName.clear();
                record.companyName.clear();
                record.fileDescription.clear();
                record.originalFilename.clear();
                record.authenticodeState = AuthenticodeState::Unavailable;
                record.signerPublisher.clear();
                incomplete = true;
                snapshot.executable.issues.append(
                        QStringLiteral("%1：元数据与 Authenticode 验证未能确认同一文件身份，已丢弃组合证据")
                                .arg(record.path));
            } else {
                record.authenticodeState = authenticodeState(signature.status);
                record.signerPublisher = signature.publisher;
                if (record.authenticodeState == AuthenticodeState::Unavailable)
                    incomplete = true;
                if (record.authenticodeState == AuthenticodeState::Unavailable
                        || record.authenticodeState == AuthenticodeState::Untrusted) {
                    snapshot.executable.issues.append(
                            QStringLiteral("%1：%2")
                                    .arg(record.path,
                                         signature.technicalDetail.isEmpty()
                                                 ? QStringLiteral("Authenticode 验证未获得可信结果")
                                                 : signature.technicalDetail));
                }
            }
        }

        if (record.pathState == ExecutablePathState::Unavailable
                || (record.pathState == ExecutablePathState::Present
                    && record.metadataState == VersionMetadataState::Unavailable)) {
            incomplete = true;
        }

        snapshot.executable.records.append(std::move(record));
    }

    const bool observedPathState = std::any_of(
            snapshot.executable.records.cbegin(),
            snapshot.executable.records.cend(),
            [](const ExecutableEvidenceRecord &record) {
        return record.pathState != ExecutablePathState::Unavailable;
    });
    if (!observedPathState && !snapshot.executable.records.isEmpty()) {
        snapshot.executable.availability = InstallationEvidenceAvailability::Unavailable;
    } else if (incomplete) {
        snapshot.executable.availability = InstallationEvidenceAvailability::Partial;
    }
}

} // namespace

InstallationEvidenceSnapshot InstallationEvidenceCollector::collect(
        const core::rules::RuleCatalog &catalog)
{
    InstallationEvidenceSnapshot snapshot;
    collectRegistryEvidence(snapshot);
    collectAppxEvidence(snapshot);
    collectExecutableEvidence(catalog, snapshot);

    logIssues(QStringLiteral("Registry"),
              snapshot.registry.availability,
              snapshot.registry.issues);
    logIssues(QStringLiteral("AppX / MSIX"),
              snapshot.appx.availability,
              snapshot.appx.issues);
    logIssues(QStringLiteral("Executable / Authenticode"),
              snapshot.executable.availability,
              snapshot.executable.issues);
    return snapshot;
}

} // namespace wam::services
