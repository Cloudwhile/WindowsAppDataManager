#pragma once

#include <QString>

#include <vector>

namespace wam::platform::windows {

struct ExecutableFileIdentity {
    bool valid = false;
    quint64 volumeSerialNumber = 0;
    quint64 fileIndex = 0;
    quint64 fileSize = 0;
    quint64 lastWriteTime = 0;

    [[nodiscard]] friend bool operator==(
            const ExecutableFileIdentity &,
            const ExecutableFileIdentity &) noexcept = default;
};

struct ExecutableFileIdentityResult {
    ExecutableFileIdentity identity;
    QString technicalDetail;
};

enum class ExecutableFileGuardState {
    Opened,
    Missing,
    Unavailable
};

class ExecutableFileGuard final {
public:
    ExecutableFileGuard() noexcept = default;
    ~ExecutableFileGuard() noexcept;

    ExecutableFileGuard(const ExecutableFileGuard &) = delete;
    ExecutableFileGuard &operator=(const ExecutableFileGuard &) = delete;

    ExecutableFileGuard(ExecutableFileGuard &&other) noexcept;
    ExecutableFileGuard &operator=(ExecutableFileGuard &&other) noexcept;

    [[nodiscard]] static ExecutableFileGuard open(const QString &path) noexcept;

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] ExecutableFileGuardState openState() const noexcept;
    [[nodiscard]] const QString &requestedPath() const noexcept;
    [[nodiscard]] const QString &finalPath() const noexcept;
    [[nodiscard]] const ExecutableFileIdentity &identity() const noexcept;
    [[nodiscard]] const QString &technicalDetail() const noexcept;
    [[nodiscard]] void *nativeHandle() const noexcept;

private:
    void close() noexcept;

    void *m_nativeHandle = nullptr;
    std::vector<void *> m_ancestorHandles;
    ExecutableFileGuardState m_openState = ExecutableFileGuardState::Unavailable;
    QString m_requestedPath;
    QString m_finalPath;
    ExecutableFileIdentity m_identity;
    QString m_technicalDetail;
};

class ExecutableFileIdentityReader final {
public:
    [[nodiscard]] static ExecutableFileIdentityResult read(
            const QString &path) noexcept;
};

} // namespace wam::platform::windows
