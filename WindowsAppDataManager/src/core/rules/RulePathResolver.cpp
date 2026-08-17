#include "RulePathResolver.h"

#include <QDir>
#include <QRegularExpression>
#include <QStringList>

namespace wam::core::rules {
namespace {

const QStringList &supportedVariables()
{
    static const QStringList variables {
        QStringLiteral("LOCALAPPDATA"),
        QStringLiteral("APPDATA"),
        QStringLiteral("USERPROFILE"),
        QStringLiteral("SystemRoot"),
        QStringLiteral("ProgramFiles"),
        QStringLiteral("ProgramFiles(x86)")
    };
    return variables;
}

QString canonicalVariable(const QString &candidate)
{
    for (const QString &variable : supportedVariables()) {
        if (candidate == variable)
            return variable;
    }
    return {};
}

bool isDriveAbsolutePath(const QString &path)
{
    if (path.size() < 3 || path.at(1) != QLatin1Char(':')
            || path.at(2) != QLatin1Char('/')) {
        return false;
    }

    const ushort drive = path.at(0).unicode();
    return (drive >= static_cast<ushort>('A') && drive <= static_cast<ushort>('Z'))
            || (drive >= static_cast<ushort>('a') && drive <= static_cast<ushort>('z'));
}

bool hasUnsafePrefix(const QString &path)
{
    const auto isSeparator = [](QChar character) {
        return character == QLatin1Char('/') || character == QLatin1Char('\\');
    };
    if (path.size() >= 2 && isSeparator(path.at(0)) && isSeparator(path.at(1)))
        return true;
    QString prefix = path.left(4);
    prefix.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return prefix.startsWith(QStringLiteral("/?/"))
            || prefix.startsWith(QStringLiteral("/./"))
            || prefix.startsWith(QStringLiteral("/??/"));
}

bool isReservedDosDeviceName(const QString &segment)
{
    const qsizetype dot = segment.indexOf(QLatin1Char('.'));
    const QString baseName = (dot < 0 ? segment : segment.left(dot)).toCaseFolded();
    static const QRegularExpression pattern(
            QStringLiteral("^(?:con|prn|aux|nul|clock\\$|conin\\$|conout\\$|(?:com|lpt)(?:[1-9]|[¹²³]))$"));
    return pattern.match(baseName).hasMatch();
}

bool hasInvalidWindowsCharacter(const QString &segment)
{
    static const QString invalidCharacters = QStringLiteral("<>\"|?*");
    for (const QChar character : segment) {
        if (character.unicode() < 32 || invalidCharacters.contains(character))
            return true;
    }
    return false;
}

bool validateSegments(const QString &path, QString *errorMessage)
{
    QString segmentPath = path;
    if (isDriveAbsolutePath(segmentPath))
        segmentPath.remove(0, 3);
    else {
        const qsizetype closingPercent = segmentPath.indexOf(QLatin1Char('%'), 1);
        if (segmentPath.startsWith(QLatin1Char('%')) && closingPercent > 0)
            segmentPath.remove(0, closingPercent + 1);
        if (segmentPath.startsWith(QLatin1Char('/')))
            segmentPath.remove(0, 1);
    }

    const QStringList segments = segmentPath.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    for (const QString &segment : segments) {
        if (segment.isEmpty())
            continue;
        if (segment == QStringLiteral("..")) {
            if (errorMessage)
                *errorMessage = QStringLiteral("路径不能包含父级跳转");
            return false;
        }
        if (segment == QStringLiteral(".")) {
            if (errorMessage)
                *errorMessage = QStringLiteral("路径不能包含当前目录跳转");
            return false;
        }
        if (segment.contains(QLatin1Char(':'))) {
            if (errorMessage)
                *errorMessage = QStringLiteral("路径包含非法冒号");
            return false;
        }
        if (segment.endsWith(QLatin1Char('.'))
                || segment.endsWith(QLatin1Char(' '))) {
            if (errorMessage)
                *errorMessage = QStringLiteral("路径段不能以点或空格结尾");
            return false;
        }
        if (isReservedDosDeviceName(segment)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("路径不能包含 DOS 保留设备名");
            return false;
        }
        if (hasInvalidWindowsCharacter(segment)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("路径包含 Windows 文件名不允许的字符");
            return false;
        }
    }
    return true;
}

} // namespace

bool validateRulePath(const QString &value, QString *errorMessage)
{
    if (value != value.trimmed()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("路径首尾不能包含空白字符");
        return false;
    }
    QString path = value;
    if (path.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("路径不能为空");
        return false;
    }
    if (hasUnsafePrefix(path)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("不允许 UNC 或设备路径");
        return false;
    }
    path = QDir::fromNativeSeparators(path);
    if (hasUnsafePrefix(path)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("不允许 UNC 或设备路径");
        return false;
    }

    static const QRegularExpression variablePattern(QStringLiteral("%([^%]+)%"));
    qsizetype variableCount = 0;
    auto matches = variablePattern.globalMatch(path);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        ++variableCount;
        const QString variable = canonicalVariable(match.captured(1));
        if (variable.isEmpty()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("包含不受支持的环境变量：%1")
                                        .arg(match.captured(0));
            return false;
        }
        if (match.capturedStart(0) != 0 || variableCount > 1) {
            if (errorMessage)
                *errorMessage = QStringLiteral("环境变量只能作为路径根目录且只能出现一次");
            return false;
        }
    }

    QString withoutVariables = path;
    withoutVariables.remove(variablePattern);
    if (withoutVariables.contains(QLatin1Char('%'))) {
        if (errorMessage)
            *errorMessage = QStringLiteral("环境变量占位符没有成对匹配");
        return false;
    }

    if (variableCount == 0 && !isDriveAbsolutePath(path)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("路径必须使用受支持的环境变量根目录或绝对盘符");
        return false;
    }
    if (variableCount == 1) {
        const qsizetype closingPercent = path.indexOf(QLatin1Char('%'), 1);
        if (closingPercent + 1 < path.size()
                && path.at(closingPercent + 1) != QLatin1Char('/')) {
            if (errorMessage)
                *errorMessage = QStringLiteral("环境变量根目录后必须使用路径分隔符");
            return false;
        }
    }

    return validateSegments(path, errorMessage);
}

RulePathResolution resolveRulePath(QString value)
{
    QString error;
    if (!validateRulePath(value, &error))
        return {RulePathResolutionStatus::Invalid, {}, error};

    value = QDir::fromNativeSeparators(value.trimmed());
    static const QRegularExpression variablePattern(QStringLiteral("^%([^%]+)%"));
    const QRegularExpressionMatch match = variablePattern.match(value);
    if (match.hasMatch()) {
        const QString variable = canonicalVariable(match.captured(1));
        const QString replacement = qEnvironmentVariable(variable.toUtf8().constData());
        if (replacement.isEmpty()) {
            return {
                RulePathResolutionStatus::MissingEnvironmentVariable,
                {},
                QStringLiteral("环境变量 %1 当前不可用").arg(variable)
            };
        }
        if (replacement != replacement.trimmed() || hasUnsafePrefix(replacement)) {
            return {
                RulePathResolutionStatus::Invalid,
                {},
                QStringLiteral("环境变量 %1 不是安全的本地绝对路径").arg(variable)
            };
        }
        const QString normalizedReplacement = QDir::fromNativeSeparators(replacement);
        QString replacementError;
        if (hasUnsafePrefix(normalizedReplacement)
                || !isDriveAbsolutePath(normalizedReplacement)
                || normalizedReplacement.contains(QLatin1Char('%'))
                || !validateSegments(normalizedReplacement, &replacementError)) {
            return {
                RulePathResolutionStatus::Invalid,
                {},
                replacementError.isEmpty()
                        ? QStringLiteral("环境变量 %1 不是安全的本地绝对路径")
                                  .arg(variable)
                        : QStringLiteral("环境变量 %1 的路径不安全：%2")
                                  .arg(variable, replacementError)
            };
        }
        value.replace(0, match.capturedLength(0), normalizedReplacement);
    }

    QString expandedError;
    if (hasUnsafePrefix(value) || !validateSegments(value, &expandedError)) {
        return {
            RulePathResolutionStatus::Invalid,
            {},
            expandedError.isEmpty()
                    ? QStringLiteral("环境变量展开后包含 UNC 或设备路径")
                    : QStringLiteral("环境变量展开后的路径不安全：%1").arg(expandedError)
        };
    }
    value = QDir::cleanPath(value);
    if (hasUnsafePrefix(value) || !isDriveAbsolutePath(value)) {
        return {
            RulePathResolutionStatus::Invalid,
            {},
            QStringLiteral("环境变量展开后不是受支持的本地绝对路径")
        };
    }
    return {RulePathResolutionStatus::Resolved, value, {}};
}

QString normalizedRulePathClaim(const QString &value)
{
    if (!validateRulePath(value))
        return {};
    return QDir::cleanPath(QDir::fromNativeSeparators(value.trimmed())).toCaseFolded();
}

QString expandRulePath(QString value)
{
    return resolveRulePath(std::move(value)).path;
}

QString normalizedPathKey(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed != path || hasUnsafePrefix(trimmed))
        return {};
    QString normalized = QDir::fromNativeSeparators(trimmed);
    if (hasUnsafePrefix(normalized) || !isDriveAbsolutePath(normalized)
            || !validateSegments(normalized, nullptr)) {
        return {};
    }
    normalized = QDir::cleanPath(normalized);
    if (!isDriveAbsolutePath(normalized) || hasUnsafePrefix(normalized))
        return {};
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}

} // namespace wam::core::rules
