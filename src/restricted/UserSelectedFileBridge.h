#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>

class QWidget;
class QFileDialog;

// Per-view, host-owned capability bridge for files explicitly selected by the
// user. The selected path never enters QML: bytes are copied into memory before
// an opaque handle and display-only metadata are returned.
class UserSelectedFileBridge : public QObject
{
    Q_OBJECT

public:
    static constexpr qint64 kMaxFileBytes = 10 * 1024 * 1024;
    static constexpr qint64 kMaxViewBytes = 40 * 1024 * 1024;
    static constexpr int kMaxHandles = 4;
    static constexpr int kChunkBytes = 32 * 1024;

    explicit UserSelectedFileBridge(QWidget* dialogParent,
                                    QObject* parent = nullptr);
    ~UserSelectedFileBridge() override;

    // Starts a non-blocking native picker and returns an opaque request ID.
    // Completion arrives through fileSelectionCompleted(). An empty ID means
    // the request was rejected before a picker could be opened.
    Q_INVOKABLE QString openFile(const QStringList& nameFilters,
                                 qint64 maxBytes);

    // Returns { base64, sequence, eof }, or an empty map for an unknown,
    // released, cross-view, or already-exhausted handle.
    Q_INVOKABLE QVariantMap readNextChunk(const QString& handle);
    Q_INVOKABLE bool release(const QString& handle);

signals:
    // selection is { handle, displayName, byteLength } on success, or an empty
    // map when the user cancels or the selected file fails validation.
    void fileSelectionCompleted(const QString& requestId,
                                const QVariantMap& selection);

protected:
    // Test seam. Production returns a native-capable QFileDialog.
    virtual QFileDialog* createFileDialog();

private:
    struct Snapshot {
        QByteArray bytes;
        qsizetype cursor = 0;
        quint64 sequence = 0;
        bool exhausted = false;
    };

    QVariantMap snapshotFile(const QString& path, qint64 maxBytes);
    void completeFileSelection(QFileDialog* dialog, int result);
    void cancelPendingSelection();
    QString createHandle() const;
    QString createRequestId() const;
    void clearSnapshots();

    QPointer<QWidget> m_dialogParent;
    QPointer<QFileDialog> m_dialog;
    QString m_requestId;
    qint64 m_requestMaxBytes = 0;
    QHash<QString, Snapshot> m_snapshots;
    qint64 m_totalBytes = 0;
};
