#pragma once

#include <QString>

namespace wam::platform::windows {

enum class PathPresenceState {
    Present,
    Missing,
    Unavailable
};

struct PathPresenceResult {
    bool supported = false;
    PathPresenceState state = PathPresenceState::Unavailable;
    bool directory = false;
    quint32 nativeError = 0;
    QString technicalDetail;
};

class PathPresenceReader final {
public:
    [[nodiscard]] static PathPresenceResult read(const QString &path) noexcept;
};

} // namespace wam::platform::windows
