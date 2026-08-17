#pragma once

#include "../filesystem/ExecutableFileIdentity.h"

#include <QString>

namespace wam::platform::windows {

enum class AuthenticodeVerificationStatus {
    Trusted,
    Unsigned,
    Untrusted,
    Unavailable
};

struct AuthenticodeVerificationResult {
    AuthenticodeVerificationStatus status = AuthenticodeVerificationStatus::Unavailable;
    ExecutableFileIdentity fileIdentity;
    bool identityStable = false;
    QString publisher;
    quint32 nativeStatus = 0;
    QString technicalDetail;
};

[[nodiscard]] AuthenticodeVerificationStatus classifyAuthenticodeStatus(
        quint32 nativeStatus,
        quint32 secondaryStatus) noexcept;

class AuthenticodeVerifier final {
public:
    [[nodiscard]] static AuthenticodeVerificationResult verify(
            const ExecutableFileGuard &guard) noexcept;
    [[nodiscard]] static AuthenticodeVerificationResult verify(
            const QString &path) noexcept;
};

} // namespace wam::platform::windows
