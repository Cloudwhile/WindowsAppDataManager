#pragma once

#include <QString>

namespace wam::platform::windows {

struct DeletionAccessResult {
    bool supported = false;
    bool available = false;
    QString path;
    quint32 nativeError = 0;
    QString technicalDetail;
};

class DeletionAccessProbe final {
public:
    [[nodiscard]] static DeletionAccessResult probe(
            const QString &path) noexcept;
};

} // namespace wam::platform::windows
