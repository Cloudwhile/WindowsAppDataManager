#pragma once

#include "ApplicationInfo.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace wam {

enum class RuleScope {
    Local,
    Roaming,
    LocalLow
};

enum class RuleIssueCode {
    JsonParse,
    InvalidRoot,
    MissingField,
    InvalidType,
    InvalidValue,
    UnsafePath,
    DuplicateId,
    ResourceUnavailable,
    AmbiguousIdentifier
};

struct RuleLocation {
    RuleScope scope = RuleScope::Local;
    QString relativePath;
};

struct RuleEntry {
    QString id;
    QString path;
    DataCategory category = DataCategory::Unknown;
    RiskLevel risk = RiskLevel::Unknown;
    RebuildableState rebuildable = RebuildableState::Unknown;
    QString impact;
};

struct RuleIdentifiers {
    QStringList registryDisplayNames;
    QStringList registryPublishers;
    QStringList appxPackageNames;
    QStringList appxPublishers;
    QStringList executableProductNames;
    QStringList executableCompanyNames;
    QStringList executableOriginalFilenames;
    QStringList authenticodePublishers;
};

struct ApplicationRule {
    QString id;
    QString version;
    QString name;
    QString publisher;
    QString category;
    QString executablePath;
    QString installPath;
    QString sourceName;
    RuleIdentifiers identifiers;
    QVector<RuleLocation> locations;
    QVector<RuleEntry> entries;
};

struct RuleLoadIssue {
    RuleIssueCode code = RuleIssueCode::InvalidValue;
    QString source;
    QString field;
    QString message;
};

} // namespace wam
