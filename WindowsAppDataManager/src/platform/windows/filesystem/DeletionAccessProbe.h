#pragma once

#include <QString>

#include <atomic>

namespace wam::platform::windows {

struct DeletionAccessResult {
    bool supported = false;
    bool available = false;
    bool cancelled = false;
    QString path;
    quint32 nativeError = 0;
    QString technicalDetail;
};

class DeletionAccessProbe final {
public:
    [[nodiscard]] static DeletionAccessResult probe(
            const QString &path,
            const std::atomic_bool &cancelRequested) noexcept;
};

} // namespace wam::platform::windows
