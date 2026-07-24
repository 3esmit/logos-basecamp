// srcdeps: CatalogResolution.cpp
//
// Catalog closure policy tests. A repository selected by the user is a source
// identity boundary: every package in its resolver request must come from that
// same repository, or the request must fail before downloader IPC.

#include "CatalogResolution.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

namespace {

QVariantMap packageRow(const QString& repositoryUrl,
                       const QString& name,
                       const QString& version,
                       const QVariantList& dependencies = {})
{
    QVariantMap manifest;
    manifest.insert(QStringLiteral("version"), version);
    manifest.insert(QStringLiteral("dependencies"), dependencies);

    QVariantMap release;
    release.insert(QStringLiteral("manifest"), manifest);

    QVariantMap row;
    row.insert(QStringLiteral("repositoryUrl"), repositoryUrl);
    row.insert(QStringLiteral("name"), name);
    row.insert(QStringLiteral("versions"), QVariantList{release});
    return row;
}

QVariantMap requestEntry(const QJsonArray& request, const QString& name)
{
    for (const QJsonValue& value : request) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("name")).toString() == name)
            return entry.toVariantMap();
    }
    return {};
}

QJsonArray parseRequest(const CatalogResolution::Plan& plan)
{
    const QJsonDocument document = QJsonDocument::fromJson(plan.dependenciesJson.toUtf8());
    Q_ASSERT(document.isArray());
    return document.array();
}

} // namespace

class CatalogResolutionTest : public QObject {
    Q_OBJECT

private slots:
    void completeClosure_staysInSelectedRepository()
    {
        const QString selected = CatalogResolution::maintainedRepositoryUrl();
        const QString official = QStringLiteral("https://packages.logos.co/logos-repo.json");
        const auto rows = CatalogResolution::indexCatalogRows({
            packageRow(selected, QStringLiteral("logos_inspector"), QStringLiteral("0.1.0"),
                       {QStringLiteral("wallet_ui")}),
            packageRow(selected, QStringLiteral("wallet_ui"), QStringLiteral("0.1.0"),
                       {QStringLiteral("wallet_module")}),
            packageRow(selected, QStringLiteral("wallet_module"), QStringLiteral("0.1.0")),
            packageRow(official, QStringLiteral("wallet_ui"), QStringLiteral("9.0.0")),
        });

        const CatalogResolution::Plan plan =
            CatalogResolution::buildPlan(QStringLiteral("logos_inspector"), selected, {}, rows);

        QVERIFY2(plan.isValid(), qPrintable(plan.error));
        QCOMPARE(plan.requiredPackages.size(), 3);

        const QJsonArray request = parseRequest(plan);
        QCOMPARE(request.size(), 3);
        for (const QJsonValue& value : request) {
            QCOMPARE(value.toObject().value(QStringLiteral("repositoryUrl")).toString(), selected);
        }
    }

    void missingSelectedRepositoryDependency_failsClosed()
    {
        const QString selected = CatalogResolution::maintainedRepositoryUrl();
        const QString official = QStringLiteral("https://packages.logos.co/logos-repo.json");
        const auto rows = CatalogResolution::indexCatalogRows({
            packageRow(selected, QStringLiteral("logos_inspector"), QStringLiteral("0.1.0"),
                       {QStringLiteral("delivery_module")}),
            packageRow(official, QStringLiteral("delivery_module"), QStringLiteral("9.0.0")),
        });

        const CatalogResolution::Plan plan =
            CatalogResolution::buildPlan(QStringLiteral("logos_inspector"), selected, {}, rows);

        QVERIFY(!plan.isValid());
        QVERIFY(plan.dependenciesJson.isEmpty());
        QVERIFY(plan.error.contains(QStringLiteral("delivery_module")));
        QVERIFY(plan.error.contains(QStringLiteral("selected repository")));
    }

    void newerOfficialDuplicate_cannotReplaceSelectedRepositoryDependency()
    {
        const QString selected = CatalogResolution::maintainedRepositoryUrl();
        const QString official = QStringLiteral("https://packages.logos.co/logos-repo.json");
        const auto rows = CatalogResolution::indexCatalogRows({
            packageRow(selected, QStringLiteral("logos_inspector"), QStringLiteral("0.1.0"),
                       {QStringLiteral("delivery_module")}),
            packageRow(selected, QStringLiteral("delivery_module"), QStringLiteral("0.1.5")),
            packageRow(official, QStringLiteral("delivery_module"), QStringLiteral("99.0.0")),
        });

        const CatalogResolution::Plan plan =
            CatalogResolution::buildPlan(QStringLiteral("logos_inspector"), selected, {}, rows);

        QVERIFY2(plan.isValid(), qPrintable(plan.error));
        const QVariantMap delivery = requestEntry(parseRequest(plan), QStringLiteral("delivery_module"));
        QCOMPARE(delivery.value(QStringLiteral("repositoryUrl")).toString(), selected);
        QVERIFY(delivery.value(QStringLiteral("repositoryUrl")).toString() != official);
    }

    void resolverRequest_neverContainsCrossRepositoryUrls()
    {
        const QString selected = CatalogResolution::maintainedRepositoryUrl();
        const QString official = QStringLiteral("https://packages.logos.co/logos-repo.json");
        const auto rows = CatalogResolution::indexCatalogRows({
            packageRow(selected, QStringLiteral("logos_inspector"), QStringLiteral("0.1.0"),
                       {QStringLiteral("storage_module"), QStringLiteral("wallet_ui")}),
            packageRow(selected, QStringLiteral("storage_module"), QStringLiteral("0.1.5")),
            packageRow(selected, QStringLiteral("wallet_ui"), QStringLiteral("0.1.0")),
            packageRow(official, QStringLiteral("storage_module"), QStringLiteral("9.0.0")),
            packageRow(official, QStringLiteral("wallet_ui"), QStringLiteral("9.0.0")),
        });

        const CatalogResolution::Plan plan = CatalogResolution::buildPlan(
            QStringLiteral("logos_inspector"), selected,
            {{QStringLiteral("storage_module"), QStringLiteral("0.1.5")}}, rows);

        QVERIFY2(plan.isValid(), qPrintable(plan.error));
        const QJsonArray request = parseRequest(plan);
        for (const QJsonValue& value : request) {
            const QString repositoryUrl =
                value.toObject().value(QStringLiteral("repositoryUrl")).toString();
            QCOMPARE(repositoryUrl, selected);
            QVERIFY(repositoryUrl != official);
        }
    }

    void resolverResponse_fromAnotherRepository_isRejected()
    {
        const QString selected = CatalogResolution::maintainedRepositoryUrl();
        const QString official = QStringLiteral("https://packages.logos.co/logos-repo.json");
        const QVariantList resolved{
            QVariantMap{
                {QStringLiteral("name"), QStringLiteral("logos_inspector")},
                {QStringLiteral("repositoryUrl"), selected},
            },
            QVariantMap{
                {QStringLiteral("name"), QStringLiteral("storage_module")},
                {QStringLiteral("repositoryUrl"), official},
            },
        };

        const QString error = CatalogResolution::validateResolvedRows(resolved, selected);

        QVERIFY(error.contains(QStringLiteral("storage_module")));
        QVERIFY(error.contains(QStringLiteral("different repository")));
    }

    void maintainedRepositoryPresence_usesExactUrlWithoutDuplicates()
    {
        const QString selected = CatalogResolution::maintainedRepositoryUrl();
        const QVariantList repositories{
            QVariantMap{{QStringLiteral("url"), QStringLiteral("https://packages.logos.co/logos-repo.json")}},
            QVariantMap{{QStringLiteral("url"), selected}},
            QVariantMap{{QStringLiteral("url"), selected}},
        };

        QVERIFY(CatalogResolution::containsRepository(repositories, selected));
        QVERIFY(!CatalogResolution::containsRepository(
            repositories, QStringLiteral("https://example.invalid/logos-repo.json")));
    }
};

QTEST_GUILESS_MAIN(CatalogResolutionTest)
#include "catalog_resolution_test.moc"
