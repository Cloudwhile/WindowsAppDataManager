#include "CleanupValidator.h"

#include "../rules/RulePathResolver.h"
#include "../scanner/DirectoryScanner.h"
#include "../../platform/windows/filesystem/DeletionAccessProbe.h"
#include "../../platform/windows/filesystem/StablePathIdentity.h"

#include <algorithm>
#include <atomic>

namespace wam::core {
namespace {

bool strictDescendantOf(const QString &path, const QString &root)
{
    const QString pathKey = rules::normalizedPathKey(path);
    const QString rootKey = rules::normalizedPathKey(root);
    return !pathKey.isEmpty() && !rootKey.isEmpty()
            && pathKey.startsWith(rootKey + QLatin1Char('/'));
}

bool insideAnyRoot(const QString &path, const QStringList &roots)
{
    return std::any_of(roots.cbegin(), roots.cend(), [&path](const QString &root) {
        return strictDescendantOf(path, root);
    });
}

bool processMatches(const QString &expectedPath,
                    const platform::windows::RunningProcessQueryResult &processes)
{
    const QString expectedKey = rules::normalizedPathKey(expectedPath);
    return !expectedKey.isEmpty()
            && std::any_of(
                    processes.processes.cbegin(), processes.processes.cend(),
                    [&expectedKey](const auto &process) {
        return rules::normalizedPathKey(process.imagePath) == expectedKey;
    });
}

bool sameIdentity(const CleanupCandidateInfo &candidate,
                  const platform::windows::StablePathIdentityResult &identity)
{
    return identity.state == platform::windows::StablePathState::Present
            && identity.identity.valid && identity.identity.directory
            && identity.identity.volumeSerialNumber == candidate.volumeSerialNumber
            && identity.identity.fileIndex == candidate.fileIndex
            && rules::normalizedPathKey(identity.finalPath)
                    == rules::normalizedPathKey(candidate.path);
}

CleanupValidationResult blocked(QString message, QString detail = {})
{
    return {CleanupValidationState::Blocked,
            std::move(message), std::move(detail)};
}

} // namespace

CleanupValidationResult CleanupValidator::validateProcessState(
        const CleanupCandidateInfo &candidate,
        const platform::windows::RunningProcessQueryResult &processes)
{
    if (!processes.supported || !processes.available || !processes.complete) {
        return blocked(QStringLiteral("无法完整确认应用当前没有运行"));
    }
    if (processMatches(candidate.executablePath, processes))
        return blocked(QStringLiteral("应用仍在运行，已取消该项清理"));
    return {CleanupValidationState::Ready,
            QStringLiteral("已再次确认应用没有运行"), {}};
}

CleanupValidationResult CleanupValidator::validate(
        const CleanupCandidateInfo &candidate,
        const QStringList &scanRoots,
        const platform::windows::RunningProcessQueryResult &processes)
{
    if (!candidate.verifiedRule
            || !candidate.ruleSource.startsWith(QStringLiteral("内置规则 / "))
            || candidate.id.isEmpty() || candidate.applicationId.isEmpty()
            || candidate.ruleEntryId.isEmpty() || candidate.executablePath.isEmpty()
            || candidate.risk != RiskLevel::Safe
            || candidate.rebuildable != RebuildableState::Yes
            || !candidate.exclusiveLocation || !candidate.scanComplete
            || candidate.containsUnsafeData || !candidate.identityValid
            || !candidate.directory || candidate.fileCount == 0
            || candidate.metadataFingerprint.isEmpty()) {
        return blocked(QStringLiteral("清理候选不再满足安全计划条件"));
    }
    if (!strictDescendantOf(candidate.path, candidate.applicationRoot)
            || !insideAnyRoot(candidate.path, scanRoots)) {
        return blocked(QStringLiteral("清理路径已超出原扫描范围"));
    }
    const CleanupValidationResult processValidation = validateProcessState(
            candidate, processes);
    if (processValidation.state != CleanupValidationState::Ready)
        return processValidation;

    const platform::windows::StablePathIdentityResult identity =
            platform::windows::StablePathIdentityReader::read(candidate.path);
    if (identity.state == platform::windows::StablePathState::Missing) {
        return {CleanupValidationState::Missing,
                QStringLiteral("目标已不存在，无需处理"), {}};
    }
    if (identity.state != platform::windows::StablePathState::Present
            || !identity.identity.valid) {
        return blocked(QStringLiteral("无法重新确认清理目标身份"),
                       identity.technicalDetail);
    }
    if (!sameIdentity(candidate, identity)) {
        return blocked(QStringLiteral("清理目标身份在扫描后发生变化"));
    }

    std::atomic_bool cancelRequested = false;
    const DirectoryScanStats stats = DirectoryScanner().scan(
            candidate.path, cancelRequested, {}, {}, {}, true);
    if (stats.cancelled || !stats.issues.isEmpty() || !stats.stabilityVerified) {
        const QString detail = stats.issues.isEmpty()
                ? QStringLiteral("无法形成稳定的二次元数据快照")
                : stats.issues.constFirst().technicalDetail;
        return blocked(QStringLiteral("清理前目录复核未完整完成"), detail);
    }
    if (stats.totalSize != candidate.size
            || stats.fileCount != candidate.fileCount
            || stats.metadataFingerprint != candidate.metadataFingerprint) {
        return blocked(QStringLiteral("目录内容在扫描后发生变化"));
    }

    const platform::windows::DeletionAccessResult deletionAccess =
            platform::windows::DeletionAccessProbe::probe(candidate.path);
    if (!deletionAccess.supported || !deletionAccess.available) {
        return blocked(QStringLiteral("目录被占用或当前没有删除权限"),
                       deletionAccess.technicalDetail);
    }

    const platform::windows::StablePathIdentityResult finalIdentity =
            platform::windows::StablePathIdentityReader::read(candidate.path);
    if (!sameIdentity(candidate, finalIdentity)) {
        return blocked(QStringLiteral("清理目标在权限检查期间发生变化"),
                       finalIdentity.technicalDetail);
    }

    return {CleanupValidationState::Ready,
            QStringLiteral("清理前安全检查已通过"), {}};
}

} // namespace wam::core
