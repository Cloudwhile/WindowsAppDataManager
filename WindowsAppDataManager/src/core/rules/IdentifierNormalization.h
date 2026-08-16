#pragma once

#include <QString>

namespace wam::core::rules {

[[nodiscard]] inline QString normalizedIdentifier(const QString &value)
{
    return value.normalized(QString::NormalizationForm_C).trimmed().toCaseFolded();
}

} // namespace wam::core::rules
