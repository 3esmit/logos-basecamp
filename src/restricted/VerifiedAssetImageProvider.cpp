#include "restricted/VerifiedAssetImageProvider.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QLoggingCategory>
#include <QMetaType>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <QVariant>

#include <utility>

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

VerifiedAssetImageProvider::VerifiedAssetImageProvider(
    QString appName, QStringList producerPersistenceRoots)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_appName(std::move(appName))
{
    QSet<QString> seen;
    for (const QString& root : producerPersistenceRoots) {
        const QString canonicalRoot = QDir(root).canonicalPath();
        if (!canonicalRoot.isEmpty() && !seen.contains(canonicalRoot)) {
            seen.insert(canonicalRoot);
            m_producerPersistenceRoots.append(canonicalRoot);
        }
    }
}

QString VerifiedAssetImageProvider::createScopedProviderName()
{
    return QString::fromLatin1(kPublicProviderName) + QLatin1Char('-')
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool VerifiedAssetImageProvider::validateProducerDeclarations(
    const QVariantList& declaredProducers,
    const QVariantList& directDependencies,
    QStringList* producers,
    QString* error)
{
    if (producers)
        producers->clear();
    if (error)
        error->clear();

    QSet<QString> directDependenciesSet;
    for (const QVariant& dependency : directDependencies) {
        if (dependency.metaType().id() == QMetaType::QString
            && isSafeAppName(dependency.toString())) {
            directDependenciesSet.insert(dependency.toString());
        }
    }

    QSet<QString> seen;
    for (const QVariant& declared : declaredProducers) {
        if (declared.metaType().id() != QMetaType::QString) {
            if (error)
                *error = QStringLiteral("verified_asset_producers entries must be strings");
            return false;
        }
        const QString producer = declared.toString();
        if (!isSafeAppName(producer) || !directDependenciesSet.contains(producer)) {
            if (error) {
                *error = QStringLiteral("verified asset producer must be a direct core dependency: ")
                    + producer;
            }
            return false;
        }
        if (seen.contains(producer)) {
            if (error)
                *error = QStringLiteral("duplicate verified asset producer: ") + producer;
            return false;
        }
        seen.insert(producer);
        if (producers)
            producers->append(producer);
    }
    return true;
}

QStringList VerifiedAssetImageProvider::producerPersistenceRoots(
    const QString& appName,
    const QStringList& producers,
    const QString& moduleDataRoot)
{
    if (!isSafeAppName(appName))
        return {};

    const QString requestedBase = moduleDataRoot.isEmpty()
        ? LogosBasecampPaths::moduleDataDirectory()
        : moduleDataRoot;
    const QString canonicalBase = QDir(requestedBase).canonicalPath();
    if (canonicalBase.isEmpty())
        return {};

    QStringList roots;
    QSet<QString> seen;
    for (const QString& producer : producers) {
        if (!isSafeAppName(producer))
            return {};
        const QString expectedProducer =
            QDir::cleanPath(canonicalBase + QLatin1Char('/') + producer);
        const QFileInfo producerInfo(expectedProducer);
        const QString canonicalProducer = producerInfo.canonicalFilePath();
        if (!producerInfo.isDir() || producerInfo.isSymLink()
            || canonicalProducer != expectedProducer
            || !isUnder(canonicalProducer, canonicalBase)) {
            continue;
        }

        const QDir producerDirectory(canonicalProducer);
        const QFileInfoList instances = producerDirectory.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& instance : instances) {
            const QString canonicalInstance = instance.canonicalFilePath();
            if (instance.isDir() && !instance.isSymLink()
                && canonicalInstance == QDir::cleanPath(instance.absoluteFilePath())
                && isUnder(canonicalInstance, canonicalProducer)
                && !seen.contains(canonicalInstance)) {
                seen.insert(canonicalInstance);
                roots.append(canonicalInstance);
            }
        }
    }
    return roots;
}

QImage VerifiedAssetImageProvider::requestImage(const QString& id, QSize* size,
                                                const QSize& requestedSize)
{
    Q_UNUSED(requestedSize);

    if (size)
        *size = {};
    if (!isSafeAppName(m_appName) || !isDigest(id) || m_producerPersistenceRoots.isEmpty())
        return {};

    for (const QString& producerRoot : m_producerPersistenceRoots) {
        const QString assetRoot = producerRoot + QStringLiteral("/verified_assets/") + m_appName;
        const QFileInfo rootInfo(assetRoot);
        const QString canonicalAssetRoot = rootInfo.canonicalFilePath();
        if (!rootInfo.isDir() || rootInfo.isSymLink()
            || canonicalAssetRoot != QDir::cleanPath(assetRoot)
            || !isUnder(canonicalAssetRoot, producerRoot)) {
            continue;
        }

        const QString assetPath = canonicalAssetRoot + QLatin1Char('/') + id
            + QStringLiteral(".png");
        const QFileInfo info(assetPath);
        if (!info.isFile() || info.size() <= 0 || info.size() > kMaxEncodedBytes)
            continue;

        const QString canonicalAssetPath = info.canonicalFilePath();
        if (!isUnder(canonicalAssetPath, canonicalAssetRoot)) {
            qCWarning(lcBasecampVerifiedAssets).noquote()
                << "Blocked verified asset outside producer root:" << assetPath;
            continue;
        }

        QFile file(canonicalAssetPath);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QByteArray bytes = file.read(kMaxEncodedBytes + 1);
        if (!file.atEnd() || bytes.isEmpty() || bytes.size() > kMaxEncodedBytes) {
            qCWarning(lcBasecampVerifiedAssets).noquote()
                << "Blocked verified asset exceeding encoded byte budget:" << assetPath;
            continue;
        }
        if (bytes.size() != info.size()
            || QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()
                != id.toLatin1()) {
            qCWarning(lcBasecampVerifiedAssets).noquote()
                << "Blocked verified asset with mismatched digest:" << assetPath;
            continue;
        }

        // Decode the exact bytes whose digest was verified. Reopening the
        // cache path here would let a concurrent replacement cross the
        // verification-to-decode boundary.
        QBuffer verifiedBytes;
        verifiedBytes.setData(bytes);
        if (!verifiedBytes.open(QIODevice::ReadOnly))
            continue;
        QImageReader reader(&verifiedBytes);
        if (!reader.canRead() || reader.format().toLower() != QByteArrayLiteral("png")) {
            qCWarning(lcBasecampVerifiedAssets).noquote()
                << "Blocked verified asset with unsupported image format:" << assetPath;
            continue;
        }
        reader.setAutoTransform(false);
        const QSize declaredSize = reader.size();
        if (!hasSafeDecodedSize(declaredSize)) {
            qCWarning(lcBasecampVerifiedAssets).noquote()
                << "Blocked verified asset exceeding decoded pixel budget:" << assetPath;
            continue;
        }

        const QImage image = reader.read();
        if (image.isNull() || !hasSafeDecodedSize(image.size()))
            continue;

        if (size)
            *size = image.size();
        return image;
    }
    return {};
}
