#pragma once

#include <QQuickImageProvider>
#include <QString>

// A QML image provider for runtime assets prepared by an application's trusted
// backend. The QML sandbox permits only image://basecamp-verified/<sha256>;
// this provider is registered once per sandboxed QML engine and is rooted in
// that application's cache directory, so one app cannot resolve another app's
// assets.
//
// Producers atomically stage a decoded PNG as <lowercase-sha256>.png in the
// root returned by assetDirectoryForApp(). The provider independently verifies
// the path, byte hash, detected format, and decoded dimensions before returning
// pixels. QML never receives a filesystem path or an arbitrary URL.
class VerifiedAssetImageProvider final : public QQuickImageProvider {
public:
    static constexpr const char* kProviderName = "basecamp-verified";

    explicit VerifiedAssetImageProvider(QString assetRoot);

    // Public cache contract for a trusted core/UI backend. This directory is
    // intentionally never added to QML's filesystem roots; it is reachable
    // only through a digest handle served by this provider.
    static QString assetDirectoryForApp(const QString& appName);

    QImage requestImage(const QString& id, QSize* size,
                        const QSize& requestedSize) override;

private:
    QString m_assetRoot;
};
