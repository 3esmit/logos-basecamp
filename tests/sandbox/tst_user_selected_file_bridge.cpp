#include <QtTest/QtTest>

#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QPointer>
#include <QQueue>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include "restricted/UserSelectedFileBridge.h"

namespace {

void writeBytes(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly),
             "cannot write test file");
    QCOMPARE(file.write(contents), contents.size());
}

class TestFileDialog final : public QFileDialog
{
public:
    void finishAccepted(const QString& path)
    {
        selectFile(path);
        done(QDialog::Accepted);
    }

    void finishRejected()
    {
        done(QDialog::Rejected);
    }
};

class TestFileBridge final : public UserSelectedFileBridge
{
public:
    enum class OutcomeType {
        Accept,
        Reject,
        Pending,
    };

    struct Outcome {
        OutcomeType type;
        QString path;
    };

    explicit TestFileBridge(QWidget* dialogParent = nullptr,
                            QObject* parent = nullptr)
        : UserSelectedFileBridge(dialogParent, parent)
    {
    }

    void enqueueSelection(const QString& path)
    {
        m_outcomes.enqueue({OutcomeType::Accept, path});
    }

    void enqueueCancellation()
    {
        m_outcomes.enqueue({OutcomeType::Reject, {}});
    }

    void enqueuePending()
    {
        m_outcomes.enqueue({OutcomeType::Pending, {}});
    }

    QPointer<QFileDialog> lastDialog;
    int dialogsCreated = 0;

protected:
    QFileDialog* createFileDialog() override
    {
        auto* dialog = new TestFileDialog;
        dialog->setOption(QFileDialog::DontUseNativeDialog);
        lastDialog = dialog;
        ++dialogsCreated;

        const Outcome outcome = m_outcomes.isEmpty()
            ? Outcome{OutcomeType::Pending, {}}
            : m_outcomes.dequeue();
        if (outcome.type == OutcomeType::Accept) {
            QTimer::singleShot(0, dialog, [dialog, path = outcome.path]() {
                dialog->finishAccepted(path);
            });
        } else if (outcome.type == OutcomeType::Reject) {
            QTimer::singleShot(0, dialog, [dialog]() {
                dialog->finishRejected();
            });
        }
        return dialog;
    }

private:
    QQueue<Outcome> m_outcomes;
};

struct Completion {
    QString requestId;
    QString completedRequestId;
    QVariantMap selection;
    bool completed = false;
};

Completion completeNext(TestFileBridge& bridge,
                        qint64 maxBytes,
                        const QStringList& nameFilters = {})
{
    QSignalSpy spy(&bridge, &UserSelectedFileBridge::fileSelectionCompleted);
    Completion completion;
    completion.requestId = bridge.openFile(nameFilters, maxBytes);
    if (completion.requestId.isEmpty())
        return completion;

    completion.completed = !spy.isEmpty() || spy.wait(2000);
    if (!completion.completed)
        return completion;

    const QVariantList arguments = spy.takeFirst();
    completion.completedRequestId = arguments.at(0).toString();
    completion.selection = arguments.at(1).toMap();
    return completion;
}

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
        bridge.enqueueCancellation();
        Completion cancellation = completeNext(bridge, 1024);
        QVERIFY(cancellation.completed);
        QCOMPARE(cancellation.completedRequestId, cancellation.requestId);
        QVERIFY(cancellation.selection.isEmpty());

        for (int i = 0; i < UserSelectedFileBridge::kMaxHandles; ++i) {
            bridge.enqueueSelection(path);
            Completion selection = completeNext(bridge, 1024);
            QVERIFY(selection.completed);
            QCOMPARE(selection.completedRequestId, selection.requestId);
            QVERIFY(!selection.selection.isEmpty());
        }

        QCOMPARE(bridge.openFile({}, 1024), QString());
        QCOMPARE(bridge.dialogsCreated,
                 UserSelectedFileBridge::kMaxHandles + 1);
    }

    void selectionCompletesAsynchronouslyWithoutExposingPath()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("selected.png"));
        const QByteArray original("original bytes");
        writeBytes(path, original);

        TestFileBridge bridge;
        bridge.enqueueSelection(path);
        QSignalSpy spy(
            &bridge, &UserSelectedFileBridge::fileSelectionCompleted);

        const QString requestId =
            bridge.openFile({QStringLiteral("Images (*.png)")}, 1024);
        QVERIFY(!requestId.isEmpty());
        QCOMPARE(spy.count(), 0);
        QVERIFY(spy.wait(2000));

        const QVariantList arguments = spy.takeFirst();
        QCOMPARE(arguments.at(0).toString(), requestId);
        const QVariantMap selection = arguments.at(1).toMap();
        QCOMPARE(selection.size(), 3);
        QVERIFY(selection.contains(QStringLiteral("handle")));
        QVERIFY(selection.contains(QStringLiteral("displayName")));
        QVERIFY(selection.contains(QStringLiteral("byteLength")));
        QCOMPARE(selection.value(QStringLiteral("displayName")).toString(),
                 QStringLiteral("selected.png"));
        QCOMPARE(selection.value(QStringLiteral("byteLength")).toLongLong(),
                 original.size());
        QVERIFY(!selection.values().contains(path));
        QVERIFY(!requestId.contains(QStringLiteral("selected")));

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
        bridge.enqueueSelection(path);
        const Completion completion = completeNext(bridge, expected.size());
        QVERIFY(completion.completed);
        const QString handle =
            completion.selection.value(QStringLiteral("handle")).toString();
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
        viewA.enqueueSelection(path);
        const Completion completion = completeNext(viewA, 1024);
        QVERIFY(completion.completed);
        const QString handle =
            completion.selection.value(QStringLiteral("handle")).toString();
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
        bridge.enqueueSelection(oversizePath);
        Completion oversize = completeNext(
            bridge, UserSelectedFileBridge::kMaxFileBytes * 2);
        QVERIFY(oversize.completed);
        QVERIFY(oversize.selection.isEmpty());

        bridge.enqueueSelection(dir.path());
        Completion nonRegular = completeNext(bridge, 1024);
        QVERIFY(nonRegular.completed);
        QVERIFY(nonRegular.selection.isEmpty());

        if (symlinkCreated) {
            QVERIFY(QFileInfo(symlinkPath).isSymLink());
            bridge.enqueueSelection(symlinkPath);
            Completion symlink = completeNext(bridge, 1024);
            QVERIFY(symlink.completed);
            QVERIFY(symlink.selection.isEmpty());
        }
    }

    void enforcesRequestedAndPerViewLimits()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("selected.bin"));
        writeBytes(path, QByteArrayLiteral("12345"));

        TestFileBridge bridge;
        QCOMPARE(bridge.openFile({}, 0), QString());
        QCOMPARE(bridge.dialogsCreated, 0);

        bridge.enqueueSelection(path);
        Completion tooSmall = completeNext(bridge, 4);
        QVERIFY(tooSmall.completed);
        QVERIFY(tooSmall.selection.isEmpty());

        for (int i = 0; i < UserSelectedFileBridge::kMaxHandles; ++i) {
            bridge.enqueueSelection(path);
            Completion selection = completeNext(bridge, 5);
            QVERIFY(selection.completed);
            QVERIFY(!selection.selection.isEmpty());
        }
        QCOMPARE(bridge.openFile({}, 5), QString());
    }

    void allowsOnlyOneActivePicker()
    {
        TestFileBridge bridge;
        bridge.enqueuePending();
        QSignalSpy spy(
            &bridge, &UserSelectedFileBridge::fileSelectionCompleted);

        const QString requestId = bridge.openFile({}, 1024);
        QVERIFY(!requestId.isEmpty());
        QCOMPARE(spy.count(), 0);
        QCOMPARE(bridge.openFile({}, 1024), QString());
        QCOMPARE(bridge.dialogsCreated, 1);

        QVERIFY(bridge.lastDialog);
        bridge.lastDialog->reject();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), requestId);
        QVERIFY(spy.at(0).at(1).toMap().isEmpty());
    }

    void viewDestructionCancelsPendingSelectionWithoutCallback()
    {
        auto* view = new QWidget;
        auto* bridge = new TestFileBridge(view, view);
        bridge->enqueuePending();
        QSignalSpy spy(
            bridge, &UserSelectedFileBridge::fileSelectionCompleted);

        QVERIFY(!bridge->openFile({}, 1024).isEmpty());
        QPointer<TestFileBridge> bridgeGuard(bridge);
        QPointer<QFileDialog> dialogGuard(bridge->lastDialog);
        QVERIFY(dialogGuard);

        delete view;
        QVERIFY(bridgeGuard.isNull());
        QVERIFY(dialogGuard.isNull());
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), 0);
    }

    void completionHandlerMayDestroyView()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("selected.bin"));
        writeBytes(path, QByteArrayLiteral("payload"));

        auto* view = new QWidget;
        auto* bridge = new TestFileBridge(view, view);
        bridge->enqueueSelection(path);
        QPointer<QWidget> viewGuard(view);
        QPointer<TestFileBridge> bridgeGuard(bridge);

        connect(bridge, &UserSelectedFileBridge::fileSelectionCompleted,
                view, [view](const QString&, const QVariantMap&) {
                    delete view;
                });

        QVERIFY(!bridge->openFile({}, 1024).isEmpty());
        QPointer<QFileDialog> dialogGuard(bridge->lastDialog);
        QTRY_VERIFY(viewGuard.isNull());
        QVERIFY(bridgeGuard.isNull());
        QTRY_VERIFY(dialogGuard.isNull());
    }
};

QTEST_MAIN(UserSelectedFileBridgeTest)
#include "tst_user_selected_file_bridge.moc"
