#pragma once

#include <QString>

namespace wam::core::rules {

class GlobMatcher final {
public:
    [[nodiscard]] static bool validate(const QString &pattern,
                                       QString *errorMessage = nullptr);
    [[nodiscard]] static bool matches(const QString &pattern,
                                      const QString &path);
};

} // namespace wam::core::rules
