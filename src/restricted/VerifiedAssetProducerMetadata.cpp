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
#include <QRegularExpression>
#include <QSet>

namespace {

constexpr qint64 kMaxInstalledMetadataBytes = 64 * 1024;
constexpr const char* kProducerField = "verified_asset_producers";
constexpr const char* kProfileField = "verified_asset_profile";
constexpr qsizetype kMaxAllowedProfiles = 16;

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

bool isSafePathSegment(const QString& value)
{
    static const QRegularExpression expression(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$"));
    return expression.match(value).hasMatch();
}

bool isSafeEnvironmentName(const QString& value)
{
    static const QRegularExpression expression(
        QStringLiteral("^[A-Z][A-Z0-9_]{0,63}$"));
    return expression.match(value).hasMatch();
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

bool stringArray(const QVariant& value,
                 const QString& field,
                 QStringList* entries,
                 QString* error)
{
    entries->clear();
    QVariantList values;
    if (value.metaType().id() == QMetaType::QVariantList) {
        values = value.toList();
    } else if (value.metaType().id() == QMetaType::QStringList) {
        for (const QString& entry : value.toStringList())
            values.append(entry);
    } else {
        return reject(field + QStringLiteral(" must be an array"), error);
    }
    if (values.isEmpty() || values.size() > kMaxAllowedProfiles) {
        return reject(field + QStringLiteral(" must contain 1 to 16 entries"), error);
    }
    QSet<QString> seen;
    for (const QVariant& value : values) {
        if (value.metaType().id() != QMetaType::QString
            || !isSafePathSegment(value.toString())) {
            return reject(field + QStringLiteral(" entries must be safe strings"), error);
        }
        if (seen.contains(value.toString())) {
            return reject(field + QStringLiteral(" entries must be unique"), error);
        }
        seen.insert(value.toString());
        entries->append(value.toString());
    }
    return true;
}

bool requiredString(const QVariantMap& object,
                    const QString& field,
                    QString* value,
                    QString* error)
{
    const QVariant candidate = object.value(field);
    if (candidate.metaType().id() != QMetaType::QString
        || candidate.toString().isEmpty()) {
        return reject(QStringLiteral("verified_asset_profile.") + field
                          + QStringLiteral(" must be a non-empty string"),
                      error);
    }
    *value = candidate.toString();
    return true;
}

bool resolveProfileRelativeRoot(const QVariant& declaration,
                                QString* profileRelativeRoot,
                                QString* error)
{
    if (declaration.metaType().id() != QMetaType::QVariantMap) {
        return reject(QStringLiteral("verified_asset_profile must be an object"), error);
    }
    const QVariantMap object = declaration.toMap();
    static const QSet<QString> allowedFields{
        QStringLiteral("directory"),
        QStringLiteral("environment"),
        QStringLiteral("default"),
        QStringLiteral("allowed_profiles"),
    };
    for (auto iterator = object.cbegin(); iterator != object.cend(); ++iterator) {
        if (!allowedFields.contains(iterator.key())) {
            return reject(QStringLiteral("verified_asset_profile contains unsupported field: ")
                              + iterator.key(),
                          error);
        }
    }

    QString directory;
    QString environment;
    QString defaultProfile;
    if (!requiredString(object, QStringLiteral("directory"), &directory, error)
        || !requiredString(object, QStringLiteral("environment"), &environment, error)
        || !requiredString(object, QStringLiteral("default"), &defaultProfile, error)) {
        return false;
    }
    if (!isSafePathSegment(directory) || !isSafeEnvironmentName(environment)
        || !isSafePathSegment(defaultProfile)) {
        return reject(QStringLiteral("verified_asset_profile contains unsafe path or environment"),
                      error);
    }

    QStringList allowedProfiles;
    if (!stringArray(object.value(QStringLiteral("allowed_profiles")),
                     QStringLiteral("verified_asset_profile.allowed_profiles"),
                     &allowedProfiles, error)) {
        return false;
    }
    if (!allowedProfiles.contains(defaultProfile)) {
        return reject(QStringLiteral("verified_asset_profile.default is not allowed"), error);
    }

    const QByteArray selectedBytes = qgetenv(environment.toLocal8Bit().constData());
    const QString selectedProfile = selectedBytes.isEmpty()
        ? defaultProfile
        : QString::fromLocal8Bit(selectedBytes);
    if (!allowedProfiles.contains(selectedProfile)) {
        return reject(QStringLiteral("selected verified asset profile is not allowed"), error);
    }

    if (profileRelativeRoot)
        *profileRelativeRoot = directory + QLatin1Char('/') + selectedProfile;
    return true;
}

bool readInstalledMetadata(const QString& installDir,
                           QVariantMap* metadata,
                           QString* error)
{
    metadata->clear();
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

    *metadata = document.object().toVariantMap();
    return true;
}

} // namespace

namespace VerifiedAssetProducerMetadata {

bool resolve(const QVariantMap& packageMetadata,
             const QVariantList& directDependencies,
             QStringList* producers,
             QString* error,
             QString* profileRelativeRoot)
{
    if (producers)
        producers->clear();
    if (error)
        error->clear();
    if (profileRelativeRoot)
        profileRelativeRoot->clear();

    QVariantList declarations;
    const QString field = QString::fromLatin1(kProducerField);
    const QString profileField = QString::fromLatin1(kProfileField);
    QVariantMap installedMetadata;
    bool installedMetadataLoaded = false;
    const auto loadInstalledMetadata = [&]() {
        if (installedMetadataLoaded)
            return true;
        if (!readInstalledMetadata(
                packageMetadata.value(QStringLiteral("installDir")).toString(),
                &installedMetadata, error)) {
            return false;
        }
        installedMetadataLoaded = true;
        return true;
    };

    const QVariantMap* producerMetadata = &packageMetadata;
    if (!packageMetadata.contains(field)) {
        if (!loadInstalledMetadata())
            return false;
        producerMetadata = &installedMetadata;
    }
    if (producerMetadata->contains(field)
        && !variantArray(producerMetadata->value(field), &declarations, error)) {
        return false;
    }

    if (!VerifiedAssetImageProvider::validateProducerDeclarations(
            declarations, directDependencies, producers, error)) {
        return false;
    }
    const QVariantMap* profileMetadata = &packageMetadata;
    if (!packageMetadata.contains(profileField)) {
        if (!installedMetadataLoaded && !packageMetadata
                                           .value(QStringLiteral("installDir"))
                                           .toString()
                                           .isEmpty()
            && !loadInstalledMetadata()) {
            return false;
        }
        if (installedMetadataLoaded)
            profileMetadata = &installedMetadata;
    }
    if (!profileMetadata->contains(profileField))
        return true;
    if (producers == nullptr || producers->isEmpty()) {
        return reject(QStringLiteral("verified_asset_profile requires declared producers"), error);
    }
    return resolveProfileRelativeRoot(
        profileMetadata->value(profileField), profileRelativeRoot, error);
}

} // namespace VerifiedAssetProducerMetadata
