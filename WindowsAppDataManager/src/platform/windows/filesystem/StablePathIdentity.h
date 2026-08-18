#pragma once

#include <QString>

namespace wam::platform::windows {

enum class StablePathState {
    Present,
    Missing,
    Unavailable
};

struct StablePathIdentity {
    bool valid = false;
    quint64 volumeSerialNumber = 0;
    quint64 fileIndex = 0;
    bool directory = false;

    [[nodiscard]] friend bool operator==(
            const StablePathIdentity &,
            const StablePathIdentity &) noexcept = default;
};

struct StablePathIdentityResult {
    StablePathState state = StablePathState::Unavailable;
    StablePathIdentity identity;
    QString finalPath;
    quint32 nativeError = 0;
    QString technicalDetail;
};

class StablePathIdentityReader final {
public:
    [[nodiscard]] static StablePathIdentityResult read(
            const QString &path) noexcept;
};

} // namespace wam::platform::windows
