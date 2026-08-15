#pragma once

#include <QStringList>

namespace wam::platform::windows {

class AppDataPaths final {
public:
    [[nodiscard]] static QStringList roots();
};

} // namespace wam::platform::windows
