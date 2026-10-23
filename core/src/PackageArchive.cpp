#include "forge/core/PackageArchive.h"

#include <zlib.h>

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QtEndian>

namespace forge::core {
bool PackageArchive::extract(const QByteArray& bytes, const QString& destination, QString* error) {
    auto fail = [error](const QString& reason) {
        if (error) *error = reason;
        return false;
    };
    if (bytes.size() > 64 * 1024 * 1024 || bytes.size() < 22) return fail("Invalid package size.");
    auto u16 = [&bytes](qsizetype at) {
        return qFromLittleEndian<quint16>(bytes.constData() + at);
    };
    auto u32 = [&bytes](qsizetype at) {
        return qFromLittleEndian<quint32>(bytes.constData() + at);
    };
    const qsizetype end = bytes.lastIndexOf(QByteArray::fromHex("504b0506"));
    if (end < 0 || end + 22 > bytes.size() || end + 22 + u16(end + 20) != bytes.size())
        return fail("Invalid ZIP directory.");
    if (u16(end + 4) || u16(end + 6) || u16(end + 8) != u16(end + 10))
        return fail("Multi-volume ZIP is unsupported.");
    const int count = u16(end + 10);
    qsizetype pos = u32(end + 16);
    const quint64 directoryEnd = quint64(pos) + u32(end + 12);
    if (count > 2048 || directoryEnd != quint64(end))
        return fail("Invalid or oversized ZIP directory.");
    quint64 total = 0;
    QSet<QString> paths;
    for (int entry = 0; entry < count; ++entry) {
        if (pos + 46 > end || u32(pos) != 0x02014b50) return fail("Corrupt ZIP entry.");
        const auto flags = u16(pos + 8);
        const auto method = u16(pos + 10);
        const quint32 expectedCrc = u32(pos + 16);
        const quint32 packed = u32(pos + 20), unpacked = u32(pos + 24);
        const int nameSize = u16(pos + 28);
        const qsizetype next = pos + 46 + nameSize + u16(pos + 30) + u16(pos + 32);
        const qsizetype local = u32(pos + 42);
        const quint32 attributes = u32(pos + 38);
        if (next > end || (flags & 1) || (method != 0 && method != 8) ||
            ((attributes >> 16) & 0170000) == 0120000)
            return fail(
                "Encrypted packages, symlinks, and this compression method are unsupported.");
        QString name = QString::fromUtf8(bytes.mid(pos + 46, nameSize));
        name.replace('\\', '/');
        if (name.isEmpty() || name.contains(QChar::Null) || name.contains(':') ||
            name.startsWith('/') || name.split('/').contains("..") ||
            paths.contains(name.toCaseFolded()))
            return fail("Unsafe or duplicate package path.");
        paths.insert(name.toCaseFolded());
        total += unpacked;
        if (unpacked > 16 * 1024 * 1024 || total > 128 * 1024 * 1024)
            return fail("Package extraction limit exceeded.");
        if (local + 30 > pos || u32(local) != 0x04034b50) return fail("Invalid local ZIP header.");
        const qsizetype body = local + 30 + u16(local + 26) + u16(local + 28);
        if (quint64(body) + packed > quint64(pos)) return fail("Truncated ZIP entry.");
        const QString path = QDir(destination).filePath(name);
        if (name.endsWith('/')) {
            if (!QDir().mkpath(path)) return fail("Cannot create package directory.");
        } else {
            QByteArray decoded;
            if (method == 0)
                decoded = bytes.mid(body, packed);
            else {
                decoded.resize(qMax(quint32(1), unpacked));
                z_stream stream{};
                stream.next_in =
                    reinterpret_cast<Bytef*>(const_cast<char*>(bytes.constData() + body));
                stream.avail_in = packed;
                stream.next_out = reinterpret_cast<Bytef*>(decoded.data());
                stream.avail_out = static_cast<uInt>(decoded.size());
                if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
                    return fail("Cannot initialize decompression.");
                const int result = inflate(&stream, Z_FINISH);
                const bool valid = result == Z_STREAM_END && stream.total_out == unpacked &&
                                   stream.total_in == packed;
                inflateEnd(&stream);
                if (!valid) return fail("Invalid compressed package entry.");
                decoded.resize(unpacked);
            }
            if (quint32(decoded.size()) != unpacked ||
                crc32(0, reinterpret_cast<const Bytef*>(decoded.constData()), unpacked) !=
                    expectedCrc)
                return fail("Package checksum mismatch.");
            if (!QDir().mkpath(QFileInfo(path).absolutePath()))
                return fail("Cannot create package directory.");
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(decoded) != decoded.size() ||
                !file.commit())
                return fail("Cannot write package file.");
        }
        pos = next;
    }
    return pos == end || fail("Invalid ZIP directory length.");
}
}  // namespace forge::core
