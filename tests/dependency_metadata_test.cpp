// Dependency manifest entries support both string and object forms. Loading
// and capability validation must resolve the same module name from either.

#include "DependencyMetadata.h"

#include <QtTest/QtTest>

class DependencyMetadataTest : public QObject {
    Q_OBJECT

private slots:
    void resolvesStringDependency()
    {
        QCOMPARE(
            DependencyMetadata::name(QStringLiteral("palace_core")),
            QStringLiteral("palace_core"));
    }

    void resolvesObjectDependency()
    {
        const QVariantMap dependency{
            {QStringLiteral("name"), QStringLiteral("palace_core")},
            {QStringLiteral("version"), QStringLiteral(">=1.0")}};
        QCOMPARE(
            DependencyMetadata::name(dependency),
            QStringLiteral("palace_core"));
    }

    void rejectsUnnamedDependency()
    {
        QCOMPARE(DependencyMetadata::name(QVariantMap{
                     {QStringLiteral("version"), QStringLiteral(">=1.0")}}),
                 QString());
        QCOMPARE(DependencyMetadata::name(QVariant(7)), QString());
    }
};

QTEST_MAIN(DependencyMetadataTest)
#include "dependency_metadata_test.moc"
