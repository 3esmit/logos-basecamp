#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>

class QWidget;

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

    // Returns { handle, displayName, byteLength }, or an empty map when the
    // picker is cancelled or the selection fails validation.
    Q_INVOKABLE QVariantMap openFile(const QStringList& nameFilters,
                                     qint64 maxBytes);

    // Returns { base64, sequence, eof }, or an empty map for an unknown,
    // released, cross-view, or already-exhausted handle.
    Q_INVOKABLE QVariantMap readNextChunk(const QString& handle);
    Q_INVOKABLE bool release(const QString& handle);

protected:
    // Test seam. Production uses QFileDialog's native existing-file picker.
    virtual QString chooseFile(const QStringList& nameFilters);

private:
    struct Snapshot {
        QByteArray bytes;
        qsizetype cursor = 0;
        quint64 sequence = 0;
        bool exhausted = false;
    };

    QVariantMap snapshotFile(const QString& path, qint64 maxBytes);
    QString createHandle() const;
    void clearSnapshots();

    QPointer<QWidget> m_dialogParent;
    QHash<QString, Snapshot> m_snapshots;
    qint64 m_totalBytes = 0;
    bool m_pickerActive = false;
};
