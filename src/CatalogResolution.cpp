#include "CatalogResolution.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QSet>
#include <QStringList>

namespace CatalogResolution {
namespace {

const QString kMaintainedRepositoryUrl = QStringLiteral(
    "https://raw.githubusercontent.com/3esmit/logos-3esmit-release/main/logos-repo.json");

QVariantMap manifestForVersion(const QVariantMap& row,
                               const QString& name,
                               const QString& requestedVersion,
                               QString* error)
{
    const QVariantList versions = row.value(QStringLiteral("versions")).toList();
    if (versions.isEmpty()) {
        *error = QStringLiteral("Package \"%1\" has no release in the selected repository.")
                     .arg(name);
        return {};
    }

    for (const QVariant& entryValue : versions) {
        const QVariantMap manifest = entryValue.toMap()
                                         .value(QStringLiteral("manifest"))
                                         .toMap();
        const QString version = manifest.value(QStringLiteral("version")).toString();
        if (requestedVersion.isEmpty() || version == requestedVersion) {
            if (!manifest.isEmpty()) return manifest;
        }
    }

    *error = QStringLiteral("Package \"%1\" version \"%2\" is not available "
                            "from the selected repository.")
                 .arg(name, requestedVersion);
    return {};
}

QString dependencyName(const QVariant& dependency)
{
    if (dependency.typeId() == QMetaType::QString) return dependency.toString();
    return dependency.toMap().value(QStringLiteral("name")).toString();
}

QVariantMap nameAndRepository(const QString& name, const QString& repositoryUrl)
{
    return {
        {QStringLiteral("name"), name},
        {QStringLiteral("repositoryUrl"), repositoryUrl},
    };
}

} // namespace

const QString& maintainedRepositoryUrl()
{
    return kMaintainedRepositoryUrl;
}

QString catalogKey(const QString& repositoryUrl, const QString& name)
{
    return repositoryUrl + QLatin1Char('\n') + name;
}

CatalogRows indexCatalogRows(const QVariantList& catalog)
{
    CatalogRows rows;
    rows.reserve(catalog.size());
    for (const QVariant& value : catalog) {
        const QVariantMap row = value.toMap();
        const QString name = row.value(QStringLiteral("name")).toString();
        const QString repositoryUrl = row.value(QStringLiteral("repositoryUrl")).toString();
        if (name.isEmpty() || repositoryUrl.isEmpty()) continue;
        rows.insert(catalogKey(repositoryUrl, name), row);
    }
    return rows;
}

bool containsRepository(const QVariantList& repositories, const QString& url)
{
    for (const QVariant& value : repositories) {
        const QVariantMap repository = value.toMap();
        QString candidate = repository.value(QStringLiteral("url")).toString();
        if (candidate.isEmpty())
            candidate = repository.value(QStringLiteral("repositoryUrl")).toString();
        if (candidate.trimmed() == url) return true;
    }
    return false;
}

Plan buildPlan(const QString& name,
               const QString& repositoryUrl,
               const QVariantMap& versionPins,
               const CatalogRows& catalogRows)
{
    Plan plan;
    if (name.isEmpty()) {
        plan.error = QStringLiteral("Select a package before resolving dependencies.");
        return plan;
    }
    if (repositoryUrl.isEmpty()) {
        plan.error = QStringLiteral("The selected package has no repository URL.");
        return plan;
    }

    QJsonArray request;
    QSet<QString> visited;
    QStringList queue{name};

    for (int index = 0; index < queue.size(); ++index) {
        const QString packageName = queue.at(index);
        if (visited.contains(packageName)) continue;

        const auto row = catalogRows.constFind(catalogKey(repositoryUrl, packageName));
        if (row == catalogRows.cend()) {
            plan.error = QStringLiteral("Package \"%1\" is required by \"%2\" but is not "
                                        "available from the selected repository.")
                             .arg(packageName, name);
            return plan;
        }

        const QString requestedVersion = versionPins.value(packageName).toString();
        QString manifestError;
        const QVariantMap manifest = manifestForVersion(
            row.value(), packageName, requestedVersion, &manifestError);
        if (manifest.isEmpty()) {
            plan.error = manifestError;
            return plan;
        }

        QJsonObject requestEntry;
        requestEntry.insert(QStringLiteral("name"), packageName);
        requestEntry.insert(QStringLiteral("repositoryUrl"), repositoryUrl);
        if (!requestedVersion.isEmpty())
            requestEntry.insert(QStringLiteral("version"), requestedVersion);
        request.append(requestEntry);
        plan.requiredPackages.append(nameAndRepository(packageName, repositoryUrl));
        visited.insert(packageName);

        for (const QVariant& dependency : manifest.value(QStringLiteral("dependencies")).toList()) {
            const QString dependencyPackage = dependencyName(dependency);
            if (dependencyPackage.isEmpty()) {
                plan.error = QStringLiteral("Package \"%1\" has an unnamed dependency in the "
                                            "selected repository.")
                                 .arg(packageName);
                return plan;
            }
            if (!visited.contains(dependencyPackage)) queue.append(dependencyPackage);
        }
    }

    for (auto pin = versionPins.cbegin(); pin != versionPins.cend(); ++pin) {
        if (!pin.value().toString().isEmpty() && !visited.contains(pin.key())) {
            plan.error = QStringLiteral("Version pin for \"%1\" is outside the selected "
                                        "repository dependency closure.")
                             .arg(pin.key());
            return plan;
        }
    }

    plan.dependenciesJson = QString::fromUtf8(
        QJsonDocument(request).toJson(QJsonDocument::Compact));
    return plan;
}

QString validateResolvedRows(const QVariantList& resolved,
                             const QString& repositoryUrl,
                             const QVariantList& requiredPackages)
{
    if (requiredPackages.isEmpty())
        return QStringLiteral("The selected repository package plan is empty.");

    QSet<QString> expected;
    expected.reserve(requiredPackages.size());
    for (const QVariant& value : requiredPackages) {
        const QVariantMap required = value.toMap();
        const QString name = required.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            return QStringLiteral("The selected repository package plan contains an unnamed package.");
        if (required.value(QStringLiteral("repositoryUrl")).toString() != repositoryUrl) {
            return QStringLiteral("Package \"%1\" is outside the selected repository plan.")
                .arg(name);
        }
        expected.insert(name);
    }

    QSet<QString> seen;
    seen.reserve(resolved.size());
    for (const QVariant& value : resolved) {
        const QVariantMap row = value.toMap();
        const QString name = row.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            return QStringLiteral("Resolver returned an unnamed package.");
        if (!expected.contains(name)) {
            return QStringLiteral("Resolver returned unexpected package \"%1\".")
                .arg(name);
        }
        if (seen.contains(name)) {
            return QStringLiteral("Resolver returned duplicate package \"%1\".")
                .arg(name);
        }
        seen.insert(name);

        const QString rowError = row.value(QStringLiteral("error")).toString();
        if (!rowError.isEmpty()) {
            return QStringLiteral("Package \"%1\" could not be resolved: %2")
                .arg(name, rowError);
        }

        const QString sourceRepository = row.value(QStringLiteral("repositoryUrl")).toString();
        if (sourceRepository != repositoryUrl) {
            return QStringLiteral("Resolver selected package \"%1\" from a different repository.")
                .arg(name);
        }
    }

    for (const QVariant& value : requiredPackages) {
        const QString name = value.toMap().value(QStringLiteral("name")).toString();
        if (!seen.contains(name)) {
            return QStringLiteral("Resolver omitted required package \"%1\".")
                .arg(name);
        }
    }

    return {};
}

} // namespace CatalogResolution
