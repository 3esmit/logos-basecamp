#include "restricted/VerifiedAssetImageProvider.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QLoggingCategory>
#include <QRegularExpression>

#include "LogosBasecampPaths.h"

Q_LOGGING_CATEGORY(lcBasecampVerifiedAssets, "logos.basecamp.verified_assets")

namespace {

constexpr qint64 kMaxEncodedBytes = 10LL * 1024 * 1024;
constexpr qint64 kMaxDecodedPixels = 16LL * 1024 * 1024;

bool isSafeAppName(const QString& appName)
{
    static const QRegularExpression expression(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$"));
    return expression.match(appName).hasMatch();
}

bool isDigest(const QString& id)
{
    static const QRegularExpression expression(QStringLiteral("^[a-f0-9]{64}$"));
    return expression.match(id).hasMatch();
}

bool isUnder(const QString& path, const QString& root)
{
    return !path.isEmpty()
        && !root.isEmpty()
        && (path == root || path.startsWith(root + QLatin1Char('/')));
}

bool hasSafeDecodedSize(const QSize& decodedSize)
{
    if (decodedSize.width() <= 0 || decodedSize.height() <= 0)
        return false;

    const qint64 pixels = static_cast<qint64>(decodedSize.width())
        * static_cast<qint64>(decodedSize.height());
    return pixels <= kMaxDecodedPixels;
}

} // namespace

VerifiedAssetImageProvider::VerifiedAssetImageProvider(QString assetRoot)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_assetRoot(QDir(assetRoot).canonicalPath())
{
}

QString VerifiedAssetImageProvider::assetDirectoryForApp(const QString& appName)
{
    if (!isSafeAppName(appName))
        return {};

    return QDir::cleanPath(
        LogosBasecampPaths::moduleDataDirectory()
        + QStringLiteral("/ui_assets/") + appName);
}

QImage VerifiedAssetImageProvider::requestImage(const QString& id, QSize* size,
                                                const QSize& requestedSize)
{
    Q_UNUSED(requestedSize);

    if (size)
        *size = {};
    if (!isDigest(id) || m_assetRoot.isEmpty())
        return {};

    const QString assetPath = m_assetRoot + QLatin1Char('/') + id
        + QStringLiteral(".png");
    const QFileInfo info(assetPath);
    if (!info.isFile() || info.size() <= 0 || info.size() > kMaxEncodedBytes)
        return {};

    const QString canonicalAssetPath = info.canonicalFilePath();
    if (!isUnder(canonicalAssetPath, m_assetRoot)) {
        qCWarning(lcBasecampVerifiedAssets).noquote()
            << "Blocked verified asset outside app cache root:" << assetPath;
        return {};
    }

    QFile file(canonicalAssetPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = file.readAll();
    if (bytes.size() != info.size()
        || QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()
            != id.toLatin1()) {
        qCWarning(lcBasecampVerifiedAssets).noquote()
            << "Blocked verified asset with mismatched digest:" << assetPath;
        return {};
    }

    QImageReader reader(canonicalAssetPath);
    if (!reader.canRead() || reader.format().toLower() != QByteArrayLiteral("png")) {
        qCWarning(lcBasecampVerifiedAssets).noquote()
            << "Blocked verified asset with unsupported image format:" << assetPath;
        return {};
    }
    reader.setAutoTransform(false);
    const QSize declaredSize = reader.size();
    if (!hasSafeDecodedSize(declaredSize)) {
        qCWarning(lcBasecampVerifiedAssets).noquote()
            << "Blocked verified asset exceeding decoded pixel budget:" << assetPath;
        return {};
    }

    const QImage image = reader.read();
    if (image.isNull() || !hasSafeDecodedSize(image.size()))
        return {};

    if (size)
        *size = image.size();
    return image;
}
