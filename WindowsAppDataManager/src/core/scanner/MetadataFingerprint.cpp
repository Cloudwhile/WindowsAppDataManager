#include "MetadataFingerprint.h"

#include <QCryptographicHash>
#include <QDir>

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <array>
#endif

namespace wam::core {

MetadataFingerprint::MetadataFingerprint()
    : m_digest(32, '\0')
{
}

void MetadataFingerprint::add(const QString &relativePath,
                              quint64 size,
                              qint64 modifiedMilliseconds)
{
    QString normalizedPath = QDir::fromNativeSeparators(relativePath).trimmed();
    while (normalizedPath.startsWith(QStringLiteral("./")))
        normalizedPath.remove(0, 2);
    normalizedPath = QDir::cleanPath(normalizedPath).toCaseFolded();

    QByteArray value = normalizedPath.toUtf8();
    value.reserve(value.size() + 48);
    value.append('\0');
    value.append(QByteArray::number(size));
    value.append('\0');
    value.append(QByteArray::number(modifiedMilliseconds));
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    std::array<char, 32> digestBuffer {};
    const QByteArrayView entryDigest = QCryptographicHash::hashInto(
            QSpan<char>(digestBuffer), value, QCryptographicHash::Sha256);
#else
    const QByteArray entryDigest = QCryptographicHash::hash(
            value, QCryptographicHash::Sha256);
#endif
    for (qsizetype index = 0; index < m_digest.size(); ++index)
        m_digest[index] = static_cast<char>(m_digest.at(index) ^ entryDigest.at(index));
    ++m_entryCount;
}

QString MetadataFingerprint::value() const
{
    if (m_entryCount == 0)
        return {};
    QByteArray digest = m_digest;
    digest.append(QByteArray::number(m_entryCount));
    return QCryptographicHash::hash(digest, QCryptographicHash::Sha256).toHex();
}

bool MetadataFingerprint::isEmpty() const noexcept
{
    return m_entryCount == 0;
}

} // namespace wam::core
