#include "GlobMatcher.h"

#include <QDir>
#include <QVector>

namespace wam::core::rules {
namespace {

QString normalized(QString value)
{
    value = QDir::fromNativeSeparators(value.trimmed()).toCaseFolded();
    while (value.startsWith(QStringLiteral("./")))
        value.remove(0, 2);
    while (value.endsWith(QLatin1Char('/')))
        value.chop(1);
    return value;
}

bool segmentMatches(const QString &pattern, const QString &value)
{
    qsizetype patternIndex = 0;
    qsizetype valueIndex = 0;
    qsizetype starIndex = -1;
    qsizetype retryIndex = -1;
    while (valueIndex < value.size()) {
        if (patternIndex < pattern.size()
                && pattern.at(patternIndex) == QLatin1Char('*')) {
            starIndex = patternIndex++;
            retryIndex = valueIndex;
        } else if (patternIndex < pattern.size()
                   && pattern.at(patternIndex) == value.at(valueIndex)) {
            ++patternIndex;
            ++valueIndex;
        } else if (starIndex >= 0) {
            patternIndex = starIndex + 1;
            valueIndex = ++retryIndex;
        } else {
            return false;
        }
    }
    while (patternIndex < pattern.size()
           && pattern.at(patternIndex) == QLatin1Char('*')) {
        ++patternIndex;
    }
    return patternIndex == pattern.size();
}

} // namespace

bool GlobMatcher::validate(const QString &pattern, QString *errorMessage)
{
    QString value = QDir::fromNativeSeparators(pattern);
    if (value.isEmpty() || value != value.trimmed()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Glob 路径不能为空或包含首尾空白");
        return false;
    }
    if (value.startsWith(QLatin1Char('/')) || value.endsWith(QLatin1Char('/'))
            || value.contains(QLatin1Char('%'))
            || value.contains(QLatin1Char('\\'))) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Glob 必须是 AppData 范围内的相对路径");
        return false;
    }

    const QStringList segments = value.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    for (const QString &segment : segments) {
        if (segment.isEmpty() || segment == QStringLiteral(".")
                || segment == QStringLiteral("..")) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Glob 不能包含空路径段或父级跳转");
            return false;
        }
        if (segment.contains(QLatin1Char('?'))
                || segment.contains(QLatin1Char('['))
                || segment.contains(QLatin1Char(']'))
                || segment.contains(QLatin1Char(':'))
                || segment.contains(QLatin1Char('<'))
                || segment.contains(QLatin1Char('>'))
                || segment.contains(QLatin1Char('"'))
                || segment.contains(QLatin1Char('|'))) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Glob 只支持 * 和 ** 通配符");
            return false;
        }
        const qsizetype doubleStar = segment.indexOf(QStringLiteral("**"));
        if (doubleStar >= 0 && segment != QStringLiteral("**")) {
            if (errorMessage)
                *errorMessage = QStringLiteral("** 必须独占一个路径段");
            return false;
        }
        if (segment.endsWith(QLatin1Char('.'))
                || segment.endsWith(QLatin1Char(' '))) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Glob 路径段不能以点或空格结尾");
            return false;
        }
    }
    return true;
}

bool GlobMatcher::matches(const QString &pattern, const QString &path)
{
    // Keep the public matcher safe even when callers bypass RuleLoader.  Glob
    // patterns are a constrained rule-language, not a general path matcher;
    // accepting an unsafe pattern here would make the safety guarantee depend
    // on every caller remembering to validate it first.
    if (!validate(pattern))
        return false;

    const QStringList patternSegments = normalized(pattern).split(
            QLatin1Char('/'), Qt::SkipEmptyParts);
    const QStringList pathSegments = normalized(path).split(
            QLatin1Char('/'), Qt::SkipEmptyParts);
    if (patternSegments.isEmpty() || pathSegments.isEmpty())
        return false;

    QVector<QVector<bool>> memo(patternSegments.size() + 1,
                                QVector<bool>(pathSegments.size() + 1, false));
    QVector<QVector<bool>> visited(patternSegments.size() + 1,
                                   QVector<bool>(pathSegments.size() + 1, false));
    const auto visit = [&](const auto &self, qsizetype patternIndex,
                           qsizetype pathIndex) -> bool {
        if (visited[patternIndex][pathIndex])
            return memo[patternIndex][pathIndex];
        visited[patternIndex][pathIndex] = true;
        bool result = false;
        if (patternIndex == patternSegments.size()) {
            result = pathIndex == pathSegments.size();
        } else if (patternSegments.at(patternIndex) == QStringLiteral("**")) {
            result = self(self, patternIndex + 1, pathIndex)
                    || (pathIndex < pathSegments.size()
                        && self(self, patternIndex, pathIndex + 1));
        } else if (pathIndex < pathSegments.size()
                   && segmentMatches(patternSegments.at(patternIndex),
                                     pathSegments.at(pathIndex))) {
            result = self(self, patternIndex + 1, pathIndex + 1);
        }
        memo[patternIndex][pathIndex] = result;
        return result;
    };
    return visit(visit, 0, 0);
}

} // namespace wam::core::rules
