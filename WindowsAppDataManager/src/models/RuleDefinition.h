#pragma once

#include "ApplicationInfo.h"
#include "RuleMetadata.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace wam {

enum class RuleScope {
    Local,
    Roaming,
    LocalLow
};

enum class RuleLocationOwnership {
    Shared,
    Exclusive
};

enum class RuleLocationRole {
    Data,
    Config,
    Cache,
    SharedData,
    InstallPayload,
    VendorNamespace,
    Mixed
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
    RuleLocationOwnership ownership = RuleLocationOwnership::Shared;
    RuleLocationRole role = RuleLocationRole::Data;
};

struct RuleEntry {
    QString id;
    QString path;
    DataCategory category = DataCategory::Unknown;
    RiskLevel risk = RiskLevel::Unknown;
    RebuildableState rebuildable = RebuildableState::Unknown;
    QString impact;
    QStringList paths;
};

struct RuleIdentifiers {
    QStringList runningProcessNames;
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
    RuleOrigin origin = RuleOrigin::BuiltIn;
    RuleTrustLevel trustLevel = RuleTrustLevel::Verified;
    RuleIdentifiers identifiers;
    QVector<RuleLocation> locations;
    QVector<RuleEntry> entries;
    QStringList executablePaths;
    QStringList installPaths;
};

struct RuleLoadIssue {
    RuleIssueCode code = RuleIssueCode::InvalidValue;
    QString source;
    QString field;
    QString message;
};

} // namespace wam
