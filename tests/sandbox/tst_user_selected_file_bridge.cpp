#include <QtTest/QtTest>

#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QQueue>
#include <QTemporaryDir>
#include <QWidget>

#include <functional>

#include "restricted/UserSelectedFileBridge.h"

namespace {

void writeBytes(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly),
             "cannot write test file");
    QCOMPARE(file.write(contents), contents.size());
}

class TestFileBridge final : public UserSelectedFileBridge
{
public:
    explicit TestFileBridge(QObject* parent = nullptr)
        : UserSelectedFileBridge(nullptr, parent)
    {
    }

    std::function<QString(const QStringList&)> picker;

protected:
    QString chooseFile(const QStringList& nameFilters) override
    {
        return picker ? picker(nameFilters) : QString();
    }
};

} // namespace

class UserSelectedFileBridgeTest : public QObject
{
    Q_OBJECT

private slots:
    void cancellationCreatesNoCapability()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("small.txt"));
        writeBytes(path, QByteArrayLiteral("small"));

        TestFileBridge bridge;
        QQueue<QString> selections;
        selections.enqueue(QString());
        for (int i = 0; i < UserSelectedFileBridge::kMaxHandles; ++i)
            selections.enqueue(path);
        bridge.picker = [&selections](const QStringList&) {
            return selections.dequeue();
        };

        QVERIFY(bridge.openFile({}, 1024).isEmpty());
        for (int i = 0; i < UserSelectedFileBridge::kMaxHandles; ++i)
            QVERIFY(!bridge.openFile({}, 1024).isEmpty());
        QVERIFY(selections.isEmpty());
    }

    void selectionExposesOnlyOpaqueMetadataAndStableSnapshot()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("selected.png"));
        const QByteArray original("original bytes");
        writeBytes(path, original);

        TestFileBridge bridge;
        bridge.picker = [path](const QStringList&) { return path; };

        const QVariantMap selection =
            bridge.openFile({QStringLiteral("Images (*.png)")}, 1024);
        QCOMPARE(selection.size(), 3);
        QVERIFY(selection.contains(QStringLiteral("handle")));
        QVERIFY(selection.contains(QStringLiteral("displayName")));
        QVERIFY(selection.contains(QStringLiteral("byteLength")));
        QCOMPARE(selection.value(QStringLiteral("displayName")).toString(),
                 QStringLiteral("selected.png"));
        QCOMPARE(selection.value(QStringLiteral("byteLength")).toLongLong(),
                 original.size());
        QVERIFY(!selection.values().contains(path));
        QVERIFY(!selection.value(QStringLiteral("handle")).toString().contains(
            QStringLiteral("selected")));

        writeBytes(path, QByteArrayLiteral("changed after selection"));
        const QVariantMap chunk = bridge.readNextChunk(
            selection.value(QStringLiteral("handle")).toString());
        QCOMPARE(QByteArray::fromBase64(
                     chunk.value(QStringLiteral("base64")).toString().toLatin1()),
                 original);
        QVERIFY(chunk.value(QStringLiteral("eof")).toBool());
    }

    void chunksAreOrderedBoundedCanonicalAndExhaust()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("large.bin"));

        QByteArray expected;
        expected.resize(UserSelectedFileBridge::kChunkBytes * 2 + 17);
        for (qsizetype i = 0; i < expected.size(); ++i)
            expected[i] = static_cast<char>(i % 251);
        writeBytes(path, expected);

        TestFileBridge bridge;
        bridge.picker = [path](const QStringList&) { return path; };
        const QString handle =
            bridge.openFile({}, expected.size())
                .value(QStringLiteral("handle")).toString();
        QVERIFY(!handle.isEmpty());

        QByteArray actual;
        for (quint64 sequence = 0; sequence < 3; ++sequence) {
            const QVariantMap chunk = bridge.readNextChunk(handle);
            QCOMPARE(chunk.size(), 3);
            QCOMPARE(chunk.value(QStringLiteral("sequence")).toULongLong(),
                     sequence);

            const QByteArray encoded =
                chunk.value(QStringLiteral("base64")).toString().toLatin1();
            const QByteArray decoded =
                QByteArray::fromBase64(
                    encoded, QByteArray::AbortOnBase64DecodingErrors);
            QVERIFY(decoded.size() <= UserSelectedFileBridge::kChunkBytes);
            QCOMPARE(decoded.toBase64(), encoded);
            actual += decoded;
            QCOMPARE(chunk.value(QStringLiteral("eof")).toBool(), sequence == 2);
        }
        QCOMPARE(actual, expected);
        QVERIFY(bridge.readNextChunk(handle).isEmpty());
    }

    void invalidCapabilitiesFailClosed()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("selected.bin"));
        writeBytes(path, QByteArrayLiteral("payload"));

        TestFileBridge viewA;
        TestFileBridge viewB;
        viewA.picker = [path](const QStringList&) { return path; };
        const QString handle =
            viewA.openFile({}, 1024)
                .value(QStringLiteral("handle")).toString();
        QVERIFY(!handle.isEmpty());

        QVERIFY(viewA.readNextChunk(QStringLiteral("forged")).isEmpty());
        QVERIFY(viewB.readNextChunk(handle).isEmpty());
        QVERIFY(!viewB.release(handle));
        QVERIFY(viewA.release(handle));
        QVERIFY(viewA.readNextChunk(handle).isEmpty());
        QVERIFY(!viewA.release(handle));
    }

    void rejectsOversizeNonRegularAndSymlinkInputs()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString oversizePath =
            dir.filePath(QStringLiteral("oversize.bin"));
        writeBytes(
            oversizePath,
            QByteArray(UserSelectedFileBridge::kMaxFileBytes + 1, 'x'));

        const QString regularPath =
            dir.filePath(QStringLiteral("regular.bin"));
        writeBytes(regularPath, QByteArrayLiteral("regular"));
        const QString symlinkPath =
            dir.filePath(QStringLiteral("symlink.bin"));
        const bool symlinkCreated = QFile::link(regularPath, symlinkPath);

        TestFileBridge bridge;
        QQueue<QString> selections;
        selections.enqueue(oversizePath);
        selections.enqueue(dir.path());
        if (symlinkCreated)
            selections.enqueue(symlinkPath);
        bridge.picker = [&selections](const QStringList&) {
            return selections.dequeue();
        };

        QVERIFY(bridge.openFile({}, UserSelectedFileBridge::kMaxFileBytes * 2)
                    .isEmpty());
        QVERIFY(bridge.openFile({}, 1024).isEmpty());
        if (symlinkCreated) {
            QVERIFY(QFileInfo(symlinkPath).isSymLink());
            QVERIFY(bridge.openFile({}, 1024).isEmpty());
        }
    }

    void enforcesRequestedAndPerViewLimits()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("selected.bin"));
        writeBytes(path, QByteArrayLiteral("12345"));

        TestFileBridge bridge;
        bridge.picker = [path](const QStringList&) { return path; };

        QVERIFY(bridge.openFile({}, 0).isEmpty());
        QVERIFY(bridge.openFile({}, 4).isEmpty());
        for (int i = 0; i < UserSelectedFileBridge::kMaxHandles; ++i)
            QVERIFY(!bridge.openFile({}, 5).isEmpty());
        QVERIFY(bridge.openFile({}, 5).isEmpty());
    }

    void allowsOnlyOneActivePicker()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("selected.bin"));
        writeBytes(path, QByteArrayLiteral("payload"));

        TestFileBridge bridge;
        bool attemptedReentry = false;
        bool reentryRejected = false;
        bridge.picker = [&bridge, &attemptedReentry, &reentryRejected,
                         path](const QStringList&) {
            attemptedReentry = true;
            reentryRejected = bridge.openFile({}, 1024).isEmpty();
            return path;
        };

        QVERIFY(!bridge.openFile({}, 1024).isEmpty());
        QVERIFY(attemptedReentry);
        QVERIFY(reentryRejected);
    }

    void bridgeDiesWithOwningView()
    {
        auto* view = new QWidget;
        auto* bridge = new UserSelectedFileBridge(view, view);
        QPointer<UserSelectedFileBridge> guard(bridge);

        delete view;
        QVERIFY(guard.isNull());
    }
};

QTEST_MAIN(UserSelectedFileBridgeTest)
#include "tst_user_selected_file_bridge.moc"
