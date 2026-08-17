#pragma once

#include <QString>
#include <QVector>

namespace wam::platform::windows {

struct RunningProcessInfo {
    quint32 processId = 0;
    QString imageName;
    QString imagePath;
};

struct RunningProcessReadIssue {
    quint32 processId = 0;
    quint32 nativeError = 0;
    QString processName;
    QString technicalDetail;
};

struct RunningProcessQueryResult {
    bool supported = false;
    bool available = false;
    bool complete = false;
    QVector<RunningProcessInfo> processes;
    QVector<RunningProcessReadIssue> issues;
};

class RunningProcessCatalog final {
public:
    [[nodiscard]] static RunningProcessQueryResult query() noexcept;
};

} // namespace wam::platform::windows
