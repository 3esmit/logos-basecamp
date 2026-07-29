#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Resolves the verified-asset producer capability for an installed ui_qml
// package. Package-manager IPC may omit extension fields from the portable LGX
// root manifest, so an absent field is recovered from the installed variant's
// metadata.json. The recovered declaration still has to name direct core
// dependencies before any persistence roots are exposed to QML.
namespace VerifiedAssetProducerMetadata {

bool resolve(const QVariantMap& packageMetadata,
             const QVariantList& directDependencies,
             QStringList* producers,
             QString* error);

} // namespace VerifiedAssetProducerMetadata
