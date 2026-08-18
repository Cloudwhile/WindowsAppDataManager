#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>

namespace wam::core {

class MetadataFingerprint final {
public:
    MetadataFingerprint();

    void add(const QString &relativePath,
             quint64 size,
             qint64 modifiedMilliseconds);

    [[nodiscard]] QString value() const;
    [[nodiscard]] bool isEmpty() const noexcept;

private:
    QByteArray m_digest;
    quint64 m_entryCount = 0;
};

} // namespace wam::core
