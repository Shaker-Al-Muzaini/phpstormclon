#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace forge::core {

enum class TextEncoding {
    Utf8,
    Utf8Bom,
    Utf16LittleEndian,
    Utf16BigEndian,
    Windows1256,
    Windows1252
};
enum class LineEnding { Lf, CrLf, Cr };

struct DecodedText {
    QString text;
    TextEncoding encoding = TextEncoding::Utf8;
    QVector<LineEnding> lineEndings;
};

class TextFileCodec final {
public:
    static bool decode(const QByteArray& bytes, DecodedText* decoded, QString* error);
    static bool encode(const QString& text, TextEncoding encoding,
                       const QVector<LineEnding>& lineEndings, QByteArray* bytes, QString* error);
    static QString encodingName(TextEncoding encoding);
    static QString lineEndingName(LineEnding lineEnding);
};

}  // namespace forge::core
