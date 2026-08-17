#include "src/core/rules/RuleCatalog.h"
#include "src/platform/windows/filesystem/ExecutableFileIdentity.h"
#include "src/platform/windows/filesystem/ExecutableMetadataReader.h"
#include "src/platform/windows/process/RunningProcessCatalog.h"
#include "src/platform/windows/security/AuthenticodeVerifier.h"
#include "src/services/InstallationEvidenceCollector.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QStringList>
#include <QtTest>

#include <algorithm>
#include <filesystem>

namespace {

QJsonObject ruleObject(const QString &executablePath)
{
    return {
        {QStringLiteral("id"), QStringLiteral("sample-app")},
        {QStringLiteral("version"), QStringLiteral("1")},
        {QStringLiteral("name"), QStringLiteral("Sample App")},
        {QStringLiteral("publisher"), QStringLiteral("Sample Publisher")},
        {QStringLiteral("applicationCategory"), QStringLiteral("工具")},
        {QStringLiteral("executablePath"), executablePath},
        {QStringLiteral("installPath"), QStringLiteral("C:/Apps/Sample")},
        {QStringLiteral("locations"), QJsonArray {
             QJsonObject {
                 {QStringLiteral("scope"), QStringLiteral("local")},
                 {QStringLiteral("path"), QStringLiteral("Sample/App Data")}
             }
         }},
        {QStringLiteral("entries"), QJsonArray {
             QJsonObject {
                 {QStringLiteral("id"), QStringLiteral("cache")},
                 {QStringLiteral("path"), QStringLiteral("Cache")},
                 {QStringLiteral("category"), QStringLiteral("cache")},
                 {QStringLiteral("risk"), QStringLiteral("safe")},
                 {QStringLiteral("rebuildable"), true},
                 {QStringLiteral("impact"), QStringLiteral("缓存可以重新生成。")}
             }
         }}
    };
}

} // namespace

class ExecutablePlatformTest final : public QObject {
    Q_OBJECT

private slots:
    void collectorRejectsHardLinkedExecutableClaims();
    void runningProcessCatalogFindsCurrentProcess();
    void metadataReaderDistinguishesMissingAndUnsignedFiles();
    void authenticodeStatusClassificationIsConservative();
    void metadataAndSignatureShareStableIdentity();
    void executableFileGuardBlocksMutation();
    void fileIdentityChangesWhenFileSizeChanges();
    void metadataReaderReadsTrustedSystemExecutable();
};

void ExecutablePlatformTest::collectorRejectsHardLinkedExecutableClaims()
{
#ifndef Q_OS_WIN
    QSKIP("Windows 文件身份仅在 Windows 上可用");
#else
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString firstPath =
            QDir(temporary.path()).filePath(QStringLiteral("first.exe"));
    const QString secondPath =
            QDir(temporary.path()).filePath(QStringLiteral("second.exe"));
    QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), firstPath));

    std::error_code linkError;
    std::filesystem::create_hard_link(
            std::filesystem::path(firstPath.toStdWString()),
            std::filesystem::path(secondPath.toStdWString()),
            linkError);
    QVERIFY2(!linkError, linkError.message().c_str());

    QJsonObject firstRule = ruleObject(firstPath);
    firstRule.insert(QStringLiteral("id"), QStringLiteral("first-app"));
    firstRule.insert(QStringLiteral("name"), QStringLiteral("First App"));
    QJsonObject secondRule = ruleObject(secondPath);
    secondRule.insert(QStringLiteral("id"), QStringLiteral("second-app"));
    secondRule.insert(QStringLiteral("name"), QStringLiteral("Second App"));
    const auto catalog = wam::core::rules::RuleCatalog::fromJsonDocuments({
        {QStringLiteral("first-hardlink.json"),
         QJsonDocument(firstRule).toJson(QJsonDocument::Compact)},
        {QStringLiteral("second-hardlink.json"),
         QJsonDocument(secondRule).toJson(QJsonDocument::Compact)}
    });
    QVERIFY(catalog.issues().isEmpty());

    const auto evidence =
            wam::services::InstallationEvidenceCollector::collect(catalog);
    QCOMPARE(evidence.executable.records.size(), 2);
    QCOMPARE(evidence.executable.availability,
             wam::InstallationEvidenceAvailability::Unavailable);
    for (const wam::ExecutableEvidenceRecord &record :
         evidence.executable.records) {
        QCOMPARE(record.pathState, wam::ExecutablePathState::Unavailable);
        QCOMPARE(record.metadataState, wam::VersionMetadataState::Unavailable);
        QCOMPARE(record.authenticodeState, wam::AuthenticodeState::Unavailable);
    }
    QVERIFY(std::any_of(
            evidence.executable.issues.cbegin(),
            evidence.executable.issues.cend(),
            [](const QString &issue) {
        return issue.contains(QStringLiteral("同一物理文件"));
    }));
#endif
}

void ExecutablePlatformTest::runningProcessCatalogFindsCurrentProcess()
{
#ifndef Q_OS_WIN
    QSKIP("Windows 运行进程目录仅在 Windows 上可用");
#else
    const auto result =
            wam::platform::windows::RunningProcessCatalog::query();
    QVERIFY(result.supported);
    QVERIFY(result.available);

    const quint32 currentProcessId = static_cast<quint32>(
            QCoreApplication::applicationPid());
    const auto iterator = std::find_if(
            result.processes.cbegin(), result.processes.cend(),
            [currentProcessId](const auto &process) {
        return process.processId == currentProcessId;
    });
    QStringList issueDetails;
    for (const auto &issue : result.issues)
        issueDetails.append(issue.technicalDetail);
    QVERIFY2(iterator != result.processes.cend(),
             qPrintable(issueDetails.join(QStringLiteral(" | "))));
    QVERIFY(!iterator->imageName.isEmpty());
    QVERIFY(!iterator->imagePath.isEmpty());

    const auto normalizedPath = [](const QString &path) {
        const QFileInfo info(path);
        const QString canonical = info.canonicalFilePath();
        return QDir::cleanPath(
                       canonical.isEmpty() ? info.absoluteFilePath() : canonical)
                .toCaseFolded();
    };
    QCOMPARE(normalizedPath(iterator->imagePath),
             normalizedPath(QCoreApplication::applicationFilePath()));
#endif
}

void ExecutablePlatformTest::metadataReaderDistinguishesMissingAndUnsignedFiles()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString missing = QDir(temporary.path()).filePath(QStringLiteral("missing.exe"));
    const auto missingResult =
            wam::platform::windows::ExecutableMetadataReader::read(missing);
    QCOMPARE(missingResult.fileState,
             wam::platform::windows::ExecutableFileState::Missing);
    QCOMPARE(missingResult.versionInfoState,
             wam::platform::windows::VersionInfoState::Missing);

    const QString invalidFile = QDir(temporary.path()).filePath(QStringLiteral("invalid.exe"));
    QFile file(invalidFile);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("not a portable executable"), qint64(25));
    file.close();

    const auto invalidSignature =
            wam::platform::windows::AuthenticodeVerifier::verify(invalidFile);
    QCOMPARE(invalidSignature.status,
             wam::platform::windows::AuthenticodeVerificationStatus::Unavailable);
    QVERIFY(!invalidSignature.technicalDetail.isEmpty());

    const QString unsignedFile = QDir(temporary.path()).filePath(QStringLiteral("unsigned.exe"));
    QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), unsignedFile));

    const auto metadata =
            wam::platform::windows::ExecutableMetadataReader::read(unsignedFile);
    QCOMPARE(metadata.fileState,
             wam::platform::windows::ExecutableFileState::Present);
    QCOMPARE(metadata.versionInfoState,
             wam::platform::windows::VersionInfoState::Missing);

    const auto signature =
            wam::platform::windows::AuthenticodeVerifier::verify(unsignedFile);
    QCOMPARE(signature.status,
             wam::platform::windows::AuthenticodeVerificationStatus::Unsigned);
    QVERIFY(signature.publisher.isEmpty());
}

void ExecutablePlatformTest::authenticodeStatusClassificationIsConservative()
{
#ifndef Q_OS_WIN
    QSKIP("WinTrust 状态仅在 Windows 上分类");
#else
    using Status = wam::platform::windows::AuthenticodeVerificationStatus;
    const auto classify = wam::platform::windows::classifyAuthenticodeStatus;

    QCOMPARE(classify(0x00000000U, 0x00000000U), Status::Trusted);
    QCOMPARE(classify(0x800B0100U, 0x800B0100U), Status::Unsigned);
    QCOMPARE(classify(0x800B0100U, 0x00000005U), Status::Unavailable);
    QCOMPARE(classify(0x80096002U, 0x00000000U), Status::Untrusted);
    QCOMPARE(classify(0x80096003U, 0x00000000U), Status::Untrusted);
    QCOMPARE(classify(0x80096005U, 0x00000000U), Status::Untrusted);
    QCOMPARE(classify(0x80096010U, 0x00000000U), Status::Untrusted);
    QCOMPARE(classify(0x800B010CU, 0x00000000U), Status::Untrusted);
    QCOMPARE(classify(0x800B010AU, 0x00000000U), Status::Unavailable);
    QCOMPARE(classify(0x80092013U, 0x00000000U), Status::Unavailable);
    QCOMPARE(classify(0x80004005U, 0x00000000U), Status::Unavailable);
#endif
}

void ExecutablePlatformTest::metadataAndSignatureShareStableIdentity()
{
#ifndef Q_OS_WIN
    QSKIP("Windows 文件身份仅在 Windows 上可用");
#else
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString executable =
            QDir(temporary.path()).filePath(QStringLiteral("identity.exe"));
    QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), executable));

    const auto metadata =
            wam::platform::windows::ExecutableMetadataReader::read(executable);
    QCOMPARE(metadata.fileState,
             wam::platform::windows::ExecutableFileState::Present);
    QVERIFY(metadata.identityStable);
    QVERIFY(metadata.fileIdentity.valid);

    const auto signature =
            wam::platform::windows::AuthenticodeVerifier::verify(executable);
    QCOMPARE(signature.status,
             wam::platform::windows::AuthenticodeVerificationStatus::Unsigned);
    QVERIFY(signature.identityStable);
    QVERIFY(signature.fileIdentity.valid);
    QCOMPARE(metadata.fileIdentity, signature.fileIdentity);
#endif
}

void ExecutablePlatformTest::executableFileGuardBlocksMutation()
{
#ifndef Q_OS_WIN
    QSKIP("Windows 稳定文件句柄仅在 Windows 上可用");
#else
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString guardedDirectory =
            QDir(temporary.path()).filePath(QStringLiteral("guard-parent"));
    QVERIFY(QDir().mkpath(guardedDirectory));
    const QString executable =
            QDir(guardedDirectory).filePath(QStringLiteral("guarded.exe"));
    QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), executable));

    QFile preexistingWriter(executable);
    QVERIFY(preexistingWriter.open(QIODevice::ReadWrite));
    const auto blockedGuard =
            wam::platform::windows::ExecutableFileGuard::open(executable);
    QVERIFY(!blockedGuard.isOpen());
    QCOMPARE(blockedGuard.openState(),
             wam::platform::windows::ExecutableFileGuardState::Unavailable);
    preexistingWriter.close();

    {
        const auto guard =
                wam::platform::windows::ExecutableFileGuard::open(executable);
        QVERIFY2(guard.isOpen(), qPrintable(guard.technicalDetail()));
        QCOMPARE(guard.openState(),
                 wam::platform::windows::ExecutableFileGuardState::Opened);
        QVERIFY(guard.identity().valid);
        QVERIFY(!guard.finalPath().isEmpty());

        QFile concurrentReader(executable);
        QVERIFY(concurrentReader.open(QIODevice::ReadOnly));
        concurrentReader.close();

        const auto metadata =
                wam::platform::windows::ExecutableMetadataReader::read(guard);
        QCOMPARE(metadata.fileState,
                 wam::platform::windows::ExecutableFileState::Present);
        QVERIFY(metadata.identityStable);
        QCOMPARE(metadata.fileIdentity, guard.identity());

        const auto signature =
                wam::platform::windows::AuthenticodeVerifier::verify(guard);
        QCOMPARE(signature.status,
                 wam::platform::windows::AuthenticodeVerificationStatus::Unsigned);
        QVERIFY(signature.identityStable);
        QCOMPARE(signature.fileIdentity, guard.identity());

        QFile concurrentWriter(executable);
        QVERIFY(!concurrentWriter.open(QIODevice::ReadWrite));
        const QString moved = executable + QStringLiteral(".moved");
        QVERIFY(!QFile::rename(executable, moved));
        QVERIFY(QFile::exists(executable));
        QVERIFY(!QFile::exists(moved));

        const QString movedDirectory = guardedDirectory
                + QStringLiteral(".moved");
        QVERIFY(!QDir().rename(guardedDirectory, movedDirectory));
        QVERIFY(QDir(guardedDirectory).exists());
        QVERIFY(!QDir(movedDirectory).exists());
    }

    QFile writerAfterRelease(executable);
    QVERIFY(writerAfterRelease.open(QIODevice::ReadWrite));
    writerAfterRelease.close();
    const QString moved = executable + QStringLiteral(".moved");
    QVERIFY(QFile::rename(executable, moved));
    QVERIFY(QFile::exists(moved));
    const QString movedDirectory = guardedDirectory + QStringLiteral(".moved");
    QVERIFY(QDir().rename(guardedDirectory, movedDirectory));
    QVERIFY(QDir(movedDirectory).exists());
#endif
}

void ExecutablePlatformTest::fileIdentityChangesWhenFileSizeChanges()
{
#ifndef Q_OS_WIN
    QSKIP("Windows 文件身份仅在 Windows 上可用");
#else
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString executable =
            QDir(temporary.path()).filePath(QStringLiteral("changing.exe"));

    QFile file(executable);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("first"), qint64(5));
    file.close();

    const auto before =
            wam::platform::windows::ExecutableFileIdentityReader::read(executable);
    QVERIFY2(before.identity.valid, qPrintable(before.technicalDetail));
    QCOMPARE(before.identity.fileSize, quint64(5));

    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write("second identity"), qint64(15));
    file.close();

    const auto after =
            wam::platform::windows::ExecutableFileIdentityReader::read(executable);
    QVERIFY2(after.identity.valid, qPrintable(after.technicalDetail));
    QCOMPARE(after.identity.fileSize, quint64(15));
    QCOMPARE(after.identity.volumeSerialNumber,
             before.identity.volumeSerialNumber);
    QCOMPARE(after.identity.fileIndex, before.identity.fileIndex);
    QVERIFY(after.identity != before.identity);
#endif
}

void ExecutablePlatformTest::metadataReaderReadsTrustedSystemExecutable()
{
#ifndef Q_OS_WIN
    QSKIP("Authenticode 仅在 Windows 上可用");
#else
    const QString systemRoot = qEnvironmentVariable("SystemRoot");
    if (systemRoot.isEmpty())
        QSKIP("系统未提供 SystemRoot");
    const QString explorer = QDir(systemRoot).filePath(QStringLiteral("explorer.exe"));
    if (!QFile::exists(explorer))
        QSKIP("系统未提供 explorer.exe");

    const auto guard =
            wam::platform::windows::ExecutableFileGuard::open(explorer);
    QVERIFY2(guard.isOpen(), qPrintable(guard.technicalDetail()));

    const auto metadata =
            wam::platform::windows::ExecutableMetadataReader::read(guard);
    QCOMPARE(metadata.fileState,
             wam::platform::windows::ExecutableFileState::Present);
    QCOMPARE(metadata.versionInfoState,
             wam::platform::windows::VersionInfoState::Available);
    QVERIFY(metadata.identityStable);
    QVERIFY(metadata.fileIdentity.valid);
    QVERIFY(!metadata.productName.isEmpty());
    QVERIFY(!metadata.companyName.isEmpty());

    const auto signature =
            wam::platform::windows::AuthenticodeVerifier::verify(guard);
    using SignatureStatus =
            wam::platform::windows::AuthenticodeVerificationStatus;
    if (signature.status == SignatureStatus::Unavailable) {
        const bool revocationInfrastructureUnavailable =
                signature.nativeStatus == 0x80092011U
                || signature.nativeStatus == 0x80092012U
                || signature.nativeStatus == 0x80092013U
                || signature.nativeStatus == 0x800B010EU;
        QVERIFY2(revocationInfrastructureUnavailable,
                 qPrintable(signature.technicalDetail));
    } else {
        QCOMPARE(signature.status, SignatureStatus::Trusted);
        QVERIFY(!signature.publisher.isEmpty());
    }
    QVERIFY(signature.identityStable);
    QVERIFY(signature.fileIdentity.valid);
    QCOMPARE(metadata.fileIdentity, signature.fileIdentity);
#endif
}

QTEST_GUILESS_MAIN(ExecutablePlatformTest)

#include "tst_executable_platform.moc"
