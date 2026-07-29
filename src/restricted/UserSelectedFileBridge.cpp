#include "restricted/UserSelectedFileBridge.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <QUuid>

#include <algorithm>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

QByteArray readRegularFile(const QString& path, qint64 maxBytes, bool* ok)
{
    *ok = false;

#ifdef Q_OS_UNIX
    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
#ifdef O_NONBLOCK
    flags |= O_NONBLOCK;
#endif

    const QByteArray nativePath = QFile::encodeName(path);
    struct stat pathStatus {};
    if (::lstat(nativePath.constData(), &pathStatus) != 0
        || S_ISLNK(pathStatus.st_mode)) {
        return {};
    }

    const int fd = ::open(nativePath.constData(), flags);
    if (fd < 0)
        return {};

    struct stat status {};
    if (::fstat(fd, &status) != 0 || !S_ISREG(status.st_mode)
        || status.st_size < 0 || status.st_size > maxBytes) {
        ::close(fd);
        return {};
    }

    QByteArray bytes;
    bytes.reserve(static_cast<qsizetype>(maxBytes + 1));
    char buffer[32 * 1024];
    qint64 remaining = maxBytes + 1;
    while (remaining > 0) {
        const size_t requestSize = static_cast<size_t>(
            std::min<qint64>(remaining, static_cast<qint64>(sizeof(buffer))));
        const ssize_t count = ::read(fd, buffer, requestSize);
        if (count == 0)
            break;
        if (count < 0) {
            if (errno == EINTR)
                continue;
            ::close(fd);
            return {};
        }
        bytes.append(buffer, static_cast<qsizetype>(count));
        remaining -= count;
    }
    ::close(fd);
#else
    const QFileInfo before(path);
    if (before.isSymLink() || !before.isFile()
        || before.size() < 0 || before.size() > maxBytes) {
        return {};
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    const QFileInfo after(path);
    if (after.isSymLink() || !after.isFile())
        return {};

    const QByteArray bytes = file.read(maxBytes + 1);
    if (file.error() != QFileDevice::NoError)
        return {};
#endif

    if (bytes.size() > maxBytes)
        return {};

    *ok = true;
    return bytes;
}

} // namespace

UserSelectedFileBridge::UserSelectedFileBridge(QWidget* dialogParent,
                                               QObject* parent)
    : QObject(parent)
    , m_dialogParent(dialogParent)
{
}

UserSelectedFileBridge::~UserSelectedFileBridge()
{
    clearSnapshots();
}

QVariantMap UserSelectedFileBridge::openFile(const QStringList& nameFilters,
                                             qint64 maxBytes)
{
    if (m_pickerActive || maxBytes <= 0
        || m_snapshots.size() >= kMaxHandles
        || m_totalBytes >= kMaxViewBytes) {
        return {};
    }

    QScopedValueRollback<bool> pickerGuard(m_pickerActive, true);
    const QString path = chooseFile(nameFilters);
    if (path.isEmpty())
        return {};

    return snapshotFile(path, std::min(maxBytes, kMaxFileBytes));
}

QVariantMap UserSelectedFileBridge::readNextChunk(const QString& handle)
{
    auto it = m_snapshots.find(handle);
    if (it == m_snapshots.end() || it->exhausted)
        return {};

    const qsizetype remaining = it->bytes.size() - it->cursor;
    const qsizetype chunkSize = std::min<qsizetype>(remaining, kChunkBytes);
    const QByteArray chunk = it->bytes.mid(it->cursor, chunkSize);
    it->cursor += chunkSize;

    const bool eof = it->cursor == it->bytes.size();
    const quint64 sequence = it->sequence++;
    it->exhausted = eof;

    return {
        {QStringLiteral("base64"),
         QString::fromLatin1(chunk.toBase64(QByteArray::Base64Encoding))},
        {QStringLiteral("sequence"), QVariant::fromValue(sequence)},
        {QStringLiteral("eof"), eof},
    };
}

bool UserSelectedFileBridge::release(const QString& handle)
{
    auto it = m_snapshots.find(handle);
    if (it == m_snapshots.end())
        return false;

    m_totalBytes -= it->bytes.size();
    it->bytes.fill('\0');
    m_snapshots.erase(it);
    return true;
}

QString UserSelectedFileBridge::chooseFile(const QStringList& nameFilters)
{
    return QFileDialog::getOpenFileName(
        m_dialogParent,
        tr("Select file"),
        QString(),
        nameFilters.join(QStringLiteral(";;")),
        nullptr,
        QFileDialog::DontResolveSymlinks);
}

QVariantMap UserSelectedFileBridge::snapshotFile(const QString& path,
                                                 qint64 maxBytes)
{
    bool ok = false;
    QByteArray bytes = readRegularFile(path, maxBytes, &ok);
    if (!ok || m_totalBytes + bytes.size() > kMaxViewBytes)
        return {};

    const QString handle = createHandle();
    if (handle.isEmpty())
        return {};

    const qint64 byteLength = bytes.size();
    Snapshot snapshot;
    snapshot.bytes = std::move(bytes);
    m_snapshots.insert(handle, std::move(snapshot));
    m_totalBytes += byteLength;

    return {
        {QStringLiteral("handle"), handle},
        {QStringLiteral("displayName"), QFileInfo(path).fileName()},
        {QStringLiteral("byteLength"), byteLength},
    };
}

QString UserSelectedFileBridge::createHandle() const
{
    for (int attempt = 0; attempt < 8; ++attempt) {
        QString handle =
            QUuid::createUuid().toString(QUuid::WithoutBraces);
        handle.remove(QLatin1Char('-'));
        if (!m_snapshots.contains(handle))
            return handle;
    }
    return {};
}

void UserSelectedFileBridge::clearSnapshots()
{
    for (Snapshot& snapshot : m_snapshots)
        snapshot.bytes.fill('\0');
    m_snapshots.clear();
    m_totalBytes = 0;
}
