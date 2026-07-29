#pragma once

#include <QQuickImageProvider>
#include <QString>
#include <QStringList>
#include <QVariantList>

// A QML image provider for runtime assets prepared by an application's trusted
// backend. The QML sandbox permits only image://basecamp-verified/<sha256>.
// Before Qt Quick sees that URL, the sandbox rewrites its provider ID to a
// per-engine value so Qt's process-global pixmap cache cannot share pixels
// between applications.
// The provider receives only persistence roots of core modules declared by the
// UI application's `verified_asset_producers` metadata field, so one app
// cannot resolve arbitrary module-data or another app's assets.
//
// A producer atomically stages a decoded PNG as <lowercase-sha256>.png under
// its own `verified_assets/<ui-app-name>/` directory. The provider
// independently verifies the path, byte hash, detected format, and decoded
// dimensions before returning pixels. QML never receives a filesystem path or
// arbitrary URL.
class VerifiedAssetImageProvider final : public QQuickImageProvider {
public:
    static constexpr const char* kPublicProviderName = "basecamp-verified";

    VerifiedAssetImageProvider(QString appName, QStringList producerPersistenceRoots);

    // Returns an unguessable provider ID for one QQmlEngine. The public QML URL
    // is rewritten to this ID by RestrictedUrlInterceptor before image loading.
    static QString createScopedProviderName();

    static bool validateProducerDeclarations(const QVariantList& declaredProducers,
                                             const QVariantList& directDependencies,
                                             QStringList* producers,
                                             QString* error);

    // Resolves only existing persistence directories belonging to the named
    // producer modules. `moduleDataRoot` is injectable for tests.
    static QStringList producerPersistenceRoots(const QString& appName,
                                                const QStringList& producers,
                                                const QString& moduleDataRoot = {});

    QImage requestImage(const QString& id, QSize* size,
                        const QSize& requestedSize) override;

private:
    QString m_appName;
    QStringList m_producerPersistenceRoots;
};
