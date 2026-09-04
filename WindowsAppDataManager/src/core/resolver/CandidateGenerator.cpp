#include "CandidateGenerator.h"

#include "AttributionScorer.h"
#include "../rules/IdentifierNormalization.h"
#include "../rules/RulePathResolver.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <utility>

namespace wam::core {
namespace {

struct CandidateAccumulator {
    struct EvidenceDetail {
        EvidenceSource source = EvidenceSource::Folder;
        QString detail;
    };

    QString name;
    QString publisher;
    QString installPath;
    int folderScore = 0;
    int registryScore = 0;
    int appxScore = 0;
    int installPathScore = 0;
    int publisherScore = 0;
    int executableScore = 0;
    int processScore = 0;
    bool registryMatched = false;
    bool appxMatched = false;
    bool executableMatched = false;
    bool processMatched = false;
    bool registryInstallPathObserved = false;
    bool appxPackageIdentityObserved = false;
    QStringList executableNames;
    QVector<EvidenceDetail> evidenceDetails;
};

QStringList tokens(QString value)
{
    value = value.normalized(QString::NormalizationForm_C).toCaseFolded();
    QStringList result = value.split(
            QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")),
            Qt::SkipEmptyParts);
    result.removeDuplicates();
    return result;
}

double nameSimilarity(const QString &left, const QString &right)
{
    const QString leftIdentifier = rules::normalizedIdentifier(left);
    const QString rightIdentifier = rules::normalizedIdentifier(right);
    if (leftIdentifier.isEmpty() || rightIdentifier.isEmpty())
        return 0.0;
    if (leftIdentifier == rightIdentifier)
        return 1.0;

    const QStringList leftTokens = tokens(left);
    const QStringList rightTokens = tokens(right);
    if (leftTokens.isEmpty() || rightTokens.isEmpty())
        return 0.0;
    int intersection = 0;
    for (const QString &token : leftTokens) {
        if (rightTokens.contains(token))
            ++intersection;
    }
    const int denominator = std::max(leftTokens.size(), rightTokens.size());
    return denominator == 0
            ? 0.0 : static_cast<double>(intersection) / denominator;
}

double installPathSimilarity(const QString &directoryPath,
                             const QString &installPath)
{
    if (installPath.trimmed().isEmpty())
        return 0.0;
    const QString directoryName = QFileInfo(directoryPath).fileName();
    const QString installName = QFileInfo(installPath).fileName();
    if (directoryName.isEmpty() || installName.isEmpty())
        return 0.0;
    return nameSimilarity(directoryName, installName);
}

QString candidateKey(const QString &name, const QString &publisher)
{
    return rules::normalizedIdentifier(name) + QLatin1Char('|')
            + rules::normalizedIdentifier(publisher);
}

QString normalizedBasename(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty())
        return {};
    return QFileInfo(QDir::fromNativeSeparators(trimmed))
            .fileName().trimmed().toCaseFolded();
}

QString executableLabel(const ExecutableEvidenceRecord &record)
{
    if (!record.productName.trimmed().isEmpty())
        return record.productName.trimmed();
    if (!record.fileDescription.trimmed().isEmpty())
        return record.fileDescription.trimmed();

    QString original = record.originalFilename.trimmed();
    if (original.isEmpty())
        original = QFileInfo(record.path).fileName().trimmed();
    const qsizetype dot = original.lastIndexOf(QLatin1Char('.'));
    if (dot > 0)
        original.truncate(dot);
    return original.trimmed();
}

QString executablePublisher(const ExecutableEvidenceRecord &record)
{
    return !record.companyName.trimmed().isEmpty()
            ? record.companyName.trimmed() : record.signerPublisher.trimmed();
}

QString executableParentLabel(const QString &path)
{
    const QFileInfo fileInfo(QDir::fromNativeSeparators(path));
    const QString parent = fileInfo.dir().dirName().trimmed();
    return parent;
}

bool isGenericExecutableLabel(const QString &label)
{
    static const QSet<QString> genericLabels {
        QStringLiteral("bootstrapper"), QStringLiteral("crashhandler"),
        QStringLiteral("helper"), QStringLiteral("host"),
        QStringLiteral("installer"), QStringLiteral("launcher"),
        QStringLiteral("service"), QStringLiteral("setup"),
        QStringLiteral("uninstaller"), QStringLiteral("update"),
        QStringLiteral("updater")
    };
    return genericLabels.contains(rules::normalizedIdentifier(label));
}

CandidateAccumulator &candidateFor(QHash<QString, CandidateAccumulator> &candidates,
                                    const QString &name,
                                    const QString &publisher,
                                    const QString &installPath)
{
    const QString key = candidateKey(name, publisher);
    auto iterator = candidates.find(key);
    if (iterator == candidates.end()) {
        iterator = candidates.insert(key, CandidateAccumulator {
            name, publisher, installPath
        });
    } else if (iterator->installPath.isEmpty() && !installPath.isEmpty()) {
        iterator->installPath = installPath;
    }
    return iterator.value();
}

void appendDetail(QVector<CandidateAccumulator::EvidenceDetail> &details,
                  EvidenceSource source,
                  const QString &detail)
{
    if (detail.isEmpty())
        return;
    const auto duplicate = std::find_if(
            details.cbegin(), details.cend(), [&source, &detail](const auto &item) {
        return item.source == source && item.detail == detail;
    });
    if (duplicate == details.cend())
        details.append({source, detail});
}

QString displayIconExecutablePath(const QString &displayIcon)
{
    QString value = displayIcon.trimmed();
    if (value.isEmpty())
        return {};

    // DisplayIcon 常见格式为“C:/Program Files/App/app.exe,0”。这里只提取
    // 路径部分，不执行任何命令，也不解析任意命令行。
    if (value.startsWith(QLatin1Char('"'))) {
        const qsizetype endQuote = value.indexOf(QLatin1Char('"'), 1);
        if (endQuote > 1)
            value = value.mid(1, endQuote - 1);
    } else {
        const qsizetype comma = value.indexOf(QLatin1Char(','));
        if (comma > 0)
            value.truncate(comma);
    }
    value = value.trimmed();
    const QFileInfo fileInfo(value);
    const QString suffix = fileInfo.suffix().toCaseFolded();
    if (fileInfo.fileName().isEmpty()
            || (suffix != QStringLiteral("exe")
                && suffix != QStringLiteral("ico"))) {
        return {};
    }
    return value;
}

QString uninstallExecutablePath(const QString &uninstallString)
{
    QString value = uninstallString.trimmed();
    if (!value.startsWith(QLatin1Char('"')))
        return {};

    const qsizetype endQuote = value.indexOf(QLatin1Char('"'), 1);
    if (endQuote <= 1)
        return {};
    value = value.mid(1, endQuote - 1).trimmed();

    const QFileInfo fileInfo(value);
    if (!fileInfo.isAbsolute() || fileInfo.suffix().compare(
                QStringLiteral("exe"), Qt::CaseInsensitive) != 0) {
        return {};
    }

    // 卸载命令只提供安装目录线索；验证为本地绝对路径后停止解析，
    // 绝不执行命令或解释其余参数。
    const QString normalized = rules::normalizedPathKey(value);
    return normalized.isEmpty() ? QString() : normalized;
}

void collectRegistryCandidates(const QString &directoryPath,
                               const InstallationEvidenceSnapshot &evidence,
                               QHash<QString, CandidateAccumulator> &candidates)
{
    const QString directoryName = QFileInfo(directoryPath).fileName();
    for (const RegistryInstallationRecord &record : evidence.registry.records) {
        if (record.systemComponent || record.displayName.trimmed().isEmpty())
            continue;
        const double nameScore = nameSimilarity(directoryName, record.displayName);
        QString installAnchor = record.installPath;
        if (installAnchor.trimmed().isEmpty()) {
            const QString iconPath = displayIconExecutablePath(record.displayIcon);
            if (!iconPath.isEmpty())
                installAnchor = QFileInfo(iconPath).absolutePath();
        }
        if (installAnchor.trimmed().isEmpty()) {
            const QString uninstallPath = uninstallExecutablePath(record.uninstallString);
            if (!uninstallPath.isEmpty())
                installAnchor = QFileInfo(uninstallPath).absolutePath();
        }
        const double pathScore = installPathSimilarity(directoryPath, installAnchor);
        if (nameScore < 0.34 && pathScore < 0.70)
            continue;

        CandidateAccumulator &candidate = candidateFor(
                candidates, record.displayName, record.publisher,
                record.installPath);
        if (candidate.installPath.isEmpty() && !installAnchor.isEmpty())
            candidate.installPath = installAnchor;
        candidate.registryMatched = true;
        candidate.registryInstallPathObserved =
                candidate.registryInstallPathObserved || pathScore >= 0.70;
        candidate.registryScore = std::max(candidate.registryScore, 30);
        candidate.folderScore = std::max(
                candidate.folderScore,
                static_cast<int>(std::lround(nameScore * 35.0)));
        candidate.installPathScore = std::max(
                candidate.installPathScore,
                static_cast<int>(std::lround(pathScore * 20.0)));
        if (!record.publisher.trimmed().isEmpty() && nameScore >= 0.50)
            candidate.publisherScore = std::max(candidate.publisherScore, 10);
        appendDetail(candidate.evidenceDetails, EvidenceSource::Registry,
                     QStringLiteral("注册表安装项提供候选名称“%1”")
                             .arg(record.displayName));
        if (!record.installPath.trimmed().isEmpty()) {
            appendDetail(candidate.evidenceDetails, EvidenceSource::InstallPath,
                         QStringLiteral("注册表 InstallLocation 提供安装目录"));
        } else if (!record.displayIcon.trimmed().isEmpty()) {
            appendDetail(candidate.evidenceDetails, EvidenceSource::InstallPath,
                         QStringLiteral("注册表 DisplayIcon 提供可验证的安装目录线索"));
        } else if (!record.uninstallString.trimmed().isEmpty()) {
            appendDetail(candidate.evidenceDetails, EvidenceSource::InstallPath,
                         QStringLiteral("注册表卸载命令中的绝对 EXE 路径提供安装目录线索"));
        }
    }
}

void collectAppxCandidates(const QString &directoryPath,
                           const InstallationEvidenceSnapshot &evidence,
                           QHash<QString, CandidateAccumulator> &candidates)
{
    const QString directoryName = QFileInfo(directoryPath).fileName();
    for (const AppxInstallationRecord &record : evidence.appx.records) {
        if (record.packageName.trimmed().isEmpty()
                && record.packageFamilyName.trimmed().isEmpty()) {
            // DisplayName 不是 AppX 身份；缺少 PackageName/FamilyName 时不能
            // 单独把同名未知目录提升为候选。
            continue;
        }
        const QString label = !record.displayName.trimmed().isEmpty()
                ? record.displayName
                : (!record.packageName.trimmed().isEmpty()
                           ? record.packageName : record.packageFamilyName);
        if (label.trimmed().isEmpty())
            continue;
        const double displayScore = nameSimilarity(directoryName, label);
        const double packageScore = nameSimilarity(directoryName, record.packageName);
        const double familyScore = nameSimilarity(directoryName,
                                                   record.packageFamilyName);
        const double pathScore = installPathSimilarity(directoryPath,
                                                       record.installPath);
        const double identityNameScore = std::max(packageScore, familyScore);
        const double nameScore = std::max(displayScore, identityNameScore);
        if (identityNameScore < 0.34 && pathScore < 0.70)
            continue;

        CandidateAccumulator &candidate = candidateFor(
                candidates, label, record.publisher, record.installPath);
        candidate.appxMatched = true;
        candidate.appxPackageIdentityObserved = candidate.appxPackageIdentityObserved
                || !record.packageName.trimmed().isEmpty()
                || !record.packageFamilyName.trimmed().isEmpty();
        candidate.appxScore = std::max(candidate.appxScore, 35);
        candidate.folderScore = std::max(
                candidate.folderScore,
                static_cast<int>(std::lround(nameScore * 35.0)));
        candidate.installPathScore = std::max(
                candidate.installPathScore,
                static_cast<int>(std::lround(pathScore * 20.0)));
        if (!record.publisher.trimmed().isEmpty() && nameScore >= 0.50)
            candidate.publisherScore = std::max(candidate.publisherScore, 10);
        appendDetail(candidate.evidenceDetails, EvidenceSource::Appx,
                     QStringLiteral("AppX 包身份提供“%1”").arg(label));
        if (!record.packageFamilyName.trimmed().isEmpty()) {
            appendDetail(candidate.evidenceDetails, EvidenceSource::Appx,
                         QStringLiteral("AppX Package Family Name 可用于交叉确认"));
        }
    }
}

void collectExecutableCandidates(
        const QString &directoryPath,
        const InstallationEvidenceSnapshot &evidence,
        QHash<QString, CandidateAccumulator> &candidates)
{
    const QString directoryName = QFileInfo(directoryPath).fileName();
    for (const ExecutableEvidenceRecord &record : evidence.executable.records) {
        if (record.pathState != ExecutablePathState::Present
                || record.metadataState != VersionMetadataState::Available) {
            // 没有稳定文件身份或版本资源的文件不能作为未知目录的归属锚点。
            continue;
        }

        const QString metadataLabel = executableLabel(record);
        const QString parentLabel = executableParentLabel(record.path);
        const double metadataScore = nameSimilarity(directoryName, metadataLabel);
        const double parentScore = nameSimilarity(directoryName, parentLabel);
        // 版本资源中的 Update/Launcher 等名称往往只是安装器组件名。
        // 只有父目录与未知目录存在明显更强的名称关系时才采用父目录，
        // 避免把任意路径段当成应用名称。
        const bool hasDescriptiveMetadata = !record.productName.trimmed().isEmpty()
                || !record.fileDescription.trimmed().isEmpty();
        const bool parentLabelPreferred = !parentLabel.isEmpty()
                && (!hasDescriptiveMetadata || isGenericExecutableLabel(metadataLabel))
                && parentScore >= 0.70
                && parentScore > metadataScore + 0.15;
        const QString label = parentLabelPreferred ? parentLabel : metadataLabel;
        if (label.isEmpty())
            continue;

        const QString executableName = normalizedBasename(record.path);
        QString executableStem = executableName;
        if (executableStem.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
            executableStem.chop(4);
        const double productScore = nameSimilarity(directoryName, metadataLabel);
        const double executableScore = nameSimilarity(directoryName, executableStem);
        const QString installAnchor = QFileInfo(record.path).absolutePath();
        const double pathScore = installPathSimilarity(directoryPath, installAnchor);
        const double folderScore = std::max({
            productScore, executableScore, parentLabelPreferred ? parentScore : 0.0
        });
        if (folderScore < 0.34 && pathScore < 0.70)
            continue;

        const QString publisher = executablePublisher(record);
        CandidateAccumulator &candidate = candidateFor(
                candidates, label, publisher, installAnchor);
        candidate.executableMatched = true;
        candidate.executableScore = std::max(candidate.executableScore, 35);
        candidate.folderScore = std::max(
                candidate.folderScore,
                static_cast<int>(std::lround(folderScore * 35.0)));
        candidate.installPathScore = std::max(
                candidate.installPathScore,
                static_cast<int>(std::lround(pathScore * 20.0)));
        if (!executableName.isEmpty()
                && !candidate.executableNames.contains(executableName)) {
            candidate.executableNames.append(executableName);
        }
        appendDetail(candidate.evidenceDetails, EvidenceSource::Executable,
                     QStringLiteral("可执行文件版本资源提供候选身份“%1”")
                             .arg(label));
        if (parentLabelPreferred) {
            appendDetail(candidate.evidenceDetails, EvidenceSource::Folder,
                         QStringLiteral("可执行文件父目录提供候选名称“%1”")
                                 .arg(parentLabel));
        }
        if (!publisher.isEmpty()) {
            candidate.publisherScore = std::max(candidate.publisherScore,
                                                folderScore >= 0.50 ? 10 : 0);
            if (folderScore >= 0.50) {
                appendDetail(candidate.evidenceDetails, EvidenceSource::Publisher,
                             QStringLiteral("可执行文件公司名提供发布者线索“%1”")
                                     .arg(publisher));
            }
        }
    }
}

void collectRunningProcessMatches(
        const InstallationEvidenceSnapshot &evidence,
        QHash<QString, CandidateAccumulator> &candidates)
{
    for (CandidateAccumulator &candidate : candidates) {
        if (!candidate.executableMatched || candidate.executableNames.isEmpty())
            continue;
        for (const RunningProcessEvidenceRecord &record
             : evidence.runningProcesses.records) {
            const QString imageName = normalizedBasename(record.imageName);
            const QString imagePathName = normalizedBasename(record.imagePath);
            if ((!imageName.isEmpty() && candidate.executableNames.contains(imageName))
                    || (!imagePathName.isEmpty()
                        && candidate.executableNames.contains(imagePathName))) {
                candidate.processMatched = true;
                candidate.processScore = 15;
                appendDetail(candidate.evidenceDetails,
                             EvidenceSource::RunningProcess,
                             QStringLiteral("运行进程与候选可执行文件名称匹配"));
                break;
            }
        }
    }
}

} // namespace

QVector<InferredApplicationCandidate> CandidateGenerator::generate(
        const QString &directoryPath,
        const InstallationEvidenceSnapshot &evidence)
{
    static const QSet<QString> vendorNamespaces {
        QStringLiteral("google"), QStringLiteral("jetbrains"),
        QStringLiteral("microsoft"), QStringLiteral("mozilla"),
        QStringLiteral("adobe"), QStringLiteral("oracle")
    };
    const QString directoryName = QFileInfo(directoryPath).fileName();
    if (vendorNamespaces.contains(rules::normalizedIdentifier(directoryName)))
        return {};

    QHash<QString, CandidateAccumulator> candidates;
    collectRegistryCandidates(directoryPath, evidence, candidates);
    collectAppxCandidates(directoryPath, evidence, candidates);
    collectExecutableCandidates(directoryPath, evidence, candidates);
    collectRunningProcessMatches(evidence, candidates);

    QVector<InferredApplicationCandidate> result;
    result.reserve(candidates.size());
    for (const CandidateAccumulator &candidate : std::as_const(candidates)) {
        const int score = std::clamp(candidate.folderScore + candidate.registryScore
                                             + candidate.appxScore
                                             + candidate.installPathScore
                                             + candidate.publisherScore
                                             + candidate.executableScore
                                             + candidate.processScore,
                                     0, 100);
        const int independentEvidenceCount =
                static_cast<int>(candidate.folderScore > 0)
                + static_cast<int>(candidate.registryMatched)
                + static_cast<int>(candidate.appxMatched)
                + static_cast<int>(candidate.installPathScore > 0)
                + static_cast<int>(candidate.executableMatched)
                + static_cast<int>(candidate.processMatched);
        const bool hasInstallationAnchor = candidate.registryMatched
                || candidate.appxMatched || candidate.executableMatched;
        if (!hasInstallationAnchor || independentEvidenceCount < 2)
            continue;
        if (candidate.appxMatched && !candidate.appxPackageIdentityObserved
                && !candidate.registryMatched)
            continue;

        // 目录名称与单条注册表 DisplayName 可能只是同名，不能据此把未知目录
        // 认定为已安装应用。Registry-only 候选至少需要 InstallLocation 或
        // DisplayIcon 提供的安装目录锚点；AppX Package Identity 本身则保留为
        // 独立的身份来源。
        if (candidate.registryMatched && !candidate.appxMatched
                && !candidate.registryInstallPathObserved)
            continue;

        InferredApplicationCandidate inferred;
        inferred.name = candidate.name;
        inferred.publisher = candidate.publisher.isEmpty()
                ? QStringLiteral("未知") : candidate.publisher;
        inferred.installPath = candidate.installPath;
        inferred.score = score;
        inferred.attribution.evidence.reserve(candidate.evidenceDetails.size() + 1);
        inferred.attribution.evidence.append({
            EvidenceSource::Folder, EvidenceStatus::Partial,
            QStringLiteral("目录名称与候选应用名称存在组合相似性")
        });
        for (const CandidateAccumulator::EvidenceDetail &item : candidate.evidenceDetails) {
            inferred.attribution.evidence.append({
                item.source, EvidenceStatus::Matched, item.detail
            });
        }
        const AttributionScoreResult attribution = AttributionScorer::evaluateInference({
            .folder = candidate.folderScore,
            .registry = candidate.registryScore,
            .appx = candidate.appxScore,
            .installPath = candidate.installPathScore,
            .publisher = candidate.publisherScore,
            .executable = candidate.executableScore,
            .process = candidate.processScore,
            .independentEvidenceCount = independentEvidenceCount,
            .margin = 100
        });
        inferred.attribution.state = attribution.state;
        inferred.attribution.confidence = attribution.confidence;
        if (candidate.registryMatched) {
            inferred.installation.evidence.append({
                EvidenceSource::Registry, EvidenceStatus::Matched,
                QStringLiteral("注册表存在对应安装项")
            });
        }
        if (candidate.appxMatched) {
            inferred.installation.evidence.append({
                EvidenceSource::Appx, EvidenceStatus::Matched,
                QStringLiteral("AppX / MSIX 包身份存在")
            });
        }
        if (candidate.executableMatched) {
            inferred.installation.evidence.append({
                EvidenceSource::Executable, EvidenceStatus::Matched,
                QStringLiteral("可执行文件版本资源存在")
            });
        }
        if (candidate.processMatched) {
            inferred.installation.evidence.append({
                EvidenceSource::RunningProcess, EvidenceStatus::Matched,
                QStringLiteral("检测到候选应用的进程正在运行")
            });
        }
        inferred.installation.state = InstallationState::Installed;
        const int installationEvidenceCount =
                static_cast<int>(candidate.registryMatched)
                + static_cast<int>(candidate.appxMatched)
                + static_cast<int>(candidate.executableMatched)
                + static_cast<int>(candidate.processMatched);
        inferred.installation.confidence = std::min(
                95, 60 + installationEvidenceCount * 15);
        result.append(std::move(inferred));
    }

    std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
        if (left.score != right.score)
            return left.score > right.score;
        return candidateKey(left.name, left.publisher)
                < candidateKey(right.name, right.publisher);
    });
    if (result.size() > 1) {
        const int margin = result.at(0).score - result.at(1).score;
        if (margin < 15) {
            for (InferredApplicationCandidate &candidate : result)
                candidate.ambiguous = true;
        }
        for (InferredApplicationCandidate &candidate : result) {
            if (candidate.ambiguous) {
                candidate.attribution.state = AttributionState::Unknown;
                candidate.attribution.confidence =
                        std::min(candidate.attribution.confidence, 40);
            }
        }
    }
    return result;
}

} // namespace wam::core
