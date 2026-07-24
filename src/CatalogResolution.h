#pragma once

#include <QHash>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace CatalogResolution {

using CatalogRows = QHash<QString, QVariantMap>;

struct Plan {
    QString dependenciesJson;
    QVariantList requiredPackages;
    QString error;

    bool isValid() const { return error.isEmpty(); }
};

const QString& maintainedRepositoryUrl();

QString catalogKey(const QString& repositoryUrl, const QString& name);
CatalogRows indexCatalogRows(const QVariantList& catalog);
bool containsRepository(const QVariantList& repositories, const QString& url);

// Build a downloader request whose complete closure is explicitly pinned to
// `repositoryUrl`. A missing package or version is rejected before IPC so the
// downloader cannot satisfy it from another enabled repository.
Plan buildPlan(const QString& name,
               const QString& repositoryUrl,
               const QVariantMap& versionPins,
               const CatalogRows& catalogRows);

// A downloader response is trusted only when every successful row identifies
// the same selected repository that was requested.
QString validateResolvedRows(const QVariantList& resolved,
                             const QString& repositoryUrl);

} // namespace CatalogResolution
