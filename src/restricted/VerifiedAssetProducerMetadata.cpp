#include "restricted/VerifiedAssetProducerMetadata.h"

#include "restricted/VerifiedAssetImageProvider.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaType>

namespace {

constexpr qint64 kMaxInstalledMetadataBytes = 64 * 1024;
constexpr const char* kProducerField = "verified_asset_producers";

bool reject(const QString& reason, QString* error)
{
    if (error)
        *error = reason;
    return false;
}

bool isUnder(const QString& path, const QString& root)
{
    return !path.isEmpty()
        && !root.isEmpty()
        && (path == root || path.startsWith(root + QLatin1Char('/')));
}

bool variantArray(const QVariant& value, QVariantList* declarations,
                  QString* error)
{
    if (value.metaType().id() == QMetaType::QVariantList) {
        *declarations = value.toList();
        return true;
    }
    if (value.metaType().id() == QMetaType::QStringList) {
        for (const QString& entry : value.toStringList())
            declarations->append(entry);
        return true;
    }
    return reject(QStringLiteral("verified_asset_producers must be an array"), error);
}

bool readInstalledDeclarations(const QString& installDir,
                               QVariantList* declarations,
                               QString* error)
{
    const QFileInfo installInfo(QDir::cleanPath(installDir));
    const QString canonicalInstallDir = installInfo.canonicalFilePath();
    if (!installInfo.isDir() || canonicalInstallDir.isEmpty())
        return reject(QStringLiteral("ui_qml installDir is not a canonical directory"), error);

    const QString metadataPath =
        QDir(canonicalInstallDir).filePath(QStringLiteral("metadata.json"));
    const QFileInfo metadataInfo(metadataPath);
    if (!metadataInfo.exists())
        return true; // No installed declaration grants no producer capability.
    if (!metadataInfo.isFile())
        return reject(QStringLiteral("installed ui_qml metadata is not a regular file"), error);

    const QString canonicalMetadataPath = metadataInfo.canonicalFilePath();
    if (!isUnder(canonicalMetadataPath, canonicalInstallDir))
        return reject(QStringLiteral("installed ui_qml metadata escapes installDir"), error);
    if (metadataInfo.isSymLink())
        return reject(QStringLiteral("installed ui_qml metadata must not be a symlink"), error);
    if (metadataInfo.size() <= 0
        || metadataInfo.size() > kMaxInstalledMetadataBytes) {
        return reject(QStringLiteral("installed ui_qml metadata exceeds size bounds"), error);
    }

    QFile metadataFile(canonicalMetadataPath);
    if (!metadataFile.open(QIODevice::ReadOnly))
        return reject(QStringLiteral("installed ui_qml metadata is not readable"), error);
    const QByteArray encoded = metadataFile.read(kMaxInstalledMetadataBytes + 1);
    if (!metadataFile.atEnd() || encoded.isEmpty()
        || encoded.size() > kMaxInstalledMetadataBytes) {
        return reject(QStringLiteral("installed ui_qml metadata exceeds size bounds"), error);
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(encoded, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return reject(QStringLiteral("installed ui_qml metadata is not a JSON object"), error);

    const QJsonValue value =
        document.object().value(QString::fromLatin1(kProducerField));
    if (value.isUndefined())
        return true;
    if (!value.isArray())
        return reject(QStringLiteral("verified_asset_producers must be an array"), error);

    *declarations = value.toArray().toVariantList();
    return true;
}

} // namespace

namespace VerifiedAssetProducerMetadata {

bool resolve(const QVariantMap& packageMetadata,
             const QVariantList& directDependencies,
             QStringList* producers,
             QString* error)
{
    if (producers)
        producers->clear();
    if (error)
        error->clear();

    QVariantList declarations;
    const QString field = QString::fromLatin1(kProducerField);
    if (packageMetadata.contains(field)) {
        if (!variantArray(packageMetadata.value(field), &declarations, error))
            return false;
    } else if (!readInstalledDeclarations(
                   packageMetadata.value(QStringLiteral("installDir")).toString(),
                   &declarations, error)) {
        return false;
    }

    return VerifiedAssetImageProvider::validateProducerDeclarations(
        declarations, directDependencies, producers, error);
}

} // namespace VerifiedAssetProducerMetadata
