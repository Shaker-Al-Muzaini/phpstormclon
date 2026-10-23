#include "forge/core/TextFileCodec.h"

#include <limits>
#include <QChar>
#include <QVector>
#include <utility>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

namespace forge::core {
namespace {

#ifdef Q_OS_WIN
bool decodeCodePage(const QByteArray& bytes, UINT codePage, QString* text) {
    if (bytes.isEmpty()) {
        text->clear();
        return true;
    }
    if (bytes.size() > std::numeric_limits<int>::max()) {
        return false;
    }
    const int byteCount = static_cast<int>(bytes.size());
    const int length = MultiByteToWideChar(codePage, 0, bytes.constData(), byteCount, nullptr, 0);
    if (length <= 0) {
        return false;
    }
    QVector<wchar_t> buffer(length);
    if (MultiByteToWideChar(codePage, 0, bytes.constData(), byteCount, buffer.data(), length) !=
        length) {
        return false;
    }
    *text = QString::fromWCharArray(buffer.constData(), length);
    return true;
}

bool encodeCodePage(const QString& text, UINT codePage, QByteArray* bytes) {
    if (text.isEmpty()) {
        bytes->clear();
        return true;
    }
    if (text.size() > std::numeric_limits<int>::max()) {
        return false;
    }
    const int characterCount = static_cast<int>(text.size());
    const auto* wideText = reinterpret_cast<const wchar_t*>(text.utf16());
    BOOL usedDefault = FALSE;
    const int length = WideCharToMultiByte(codePage, WC_NO_BEST_FIT_CHARS, wideText, characterCount,
                                           nullptr, 0, nullptr, &usedDefault);
    if (length <= 0 || usedDefault) {
        return false;
    }
    bytes->resize(length);
    usedDefault = FALSE;
    if (WideCharToMultiByte(codePage, WC_NO_BEST_FIT_CHARS, wideText, characterCount, bytes->data(),
                            length, nullptr, &usedDefault) != length ||
        usedDefault) {
        bytes->clear();
        return false;
    }
    return true;
}

#endif

bool isArabicText(const QString& text) {
    int arabicCharacters = 0;
    int latinCharacters = 0;
    for (QChar character : text) {
        const ushort value = character.unicode();
        if ((value >= 0x0600 && value <= 0x06ff) || (value >= 0x0750 && value <= 0x077f) ||
            (value >= 0x08a0 && value <= 0x08ff)) {
            ++arabicCharacters;
        } else if (character.isLetter() && value < 0x0600) {
            ++latinCharacters;
        }
    }
    return arabicCharacters > 0 && arabicCharacters >= latinCharacters;
}

QString decodeUtf8(const QByteArray& bytes, bool* valid) {
    const QString text = QString::fromUtf8(bytes.constData(), bytes.size());
    *valid = text.toUtf8() == bytes;
    return text;
}

void normalizeLineEndings(const QString& input, DecodedText* decoded) {
    decoded->text.clear();
    decoded->lineEndings.clear();
    decoded->text.reserve(input.size());
    for (qsizetype index = 0; index < input.size(); ++index) {
        const QChar character = input.at(index);
        if (character == QLatin1Char('\r')) {
            if (index + 1 < input.size() && input.at(index + 1) == QLatin1Char('\n')) {
                ++index;
                decoded->lineEndings.append(LineEnding::CrLf);
            } else {
                decoded->lineEndings.append(LineEnding::Cr);
            }
            decoded->text.append(QLatin1Char('\n'));
        } else if (character == QLatin1Char('\n')) {
            decoded->lineEndings.append(LineEnding::Lf);
            decoded->text.append(QLatin1Char('\n'));
        } else {
            decoded->text.append(character);
        }
    }
}

LineEnding dominantLineEnding(const QVector<LineEnding>& endings) {
    int lf = 0;
    int crlf = 0;
    int cr = 0;
    for (LineEnding ending : endings) {
        switch (ending) {
            case LineEnding::Lf:
                ++lf;
                break;
            case LineEnding::CrLf:
                ++crlf;
                break;
            case LineEnding::Cr:
                ++cr;
                break;
        }
    }
    if (crlf >= lf && crlf >= cr && crlf > 0) {
        return LineEnding::CrLf;
    }
    if (cr > lf && cr > 0) {
        return LineEnding::Cr;
    }
    return LineEnding::Lf;
}

}  // namespace

bool TextFileCodec::decode(const QByteArray& bytes, DecodedText* decoded, QString* error) {
    if (!decoded) {
        if (error) {
            *error = QStringLiteral("No output provided for decoded text.");
        }
        return false;
    }

    QString text;
    TextEncoding encoding = TextEncoding::Utf8;
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        bool valid = false;
        text = decodeUtf8(bytes.mid(3), &valid);
        if (!valid) {
            if (error) {
                *error = QStringLiteral("The file has a UTF-8 BOM but contains invalid UTF-8.");
            }
            return false;
        }
        encoding = TextEncoding::Utf8Bom;
    } else if (bytes.startsWith("\xFF\xFE") || bytes.startsWith("\xFE\xFF")) {
        const bool littleEndian = bytes.startsWith("\xFF\xFE");
        const QByteArray payload = bytes.mid(2);
        if (payload.size() % 2 != 0) {
            if (error) {
                *error = QStringLiteral("The UTF-16 file contains an incomplete code unit.");
            }
            return false;
        }
        QVector<char16_t> codeUnits;
        codeUnits.reserve(payload.size() / 2);
        for (qsizetype index = 0; index < payload.size(); index += 2) {
            const auto first = static_cast<uchar>(payload.at(index));
            const auto second = static_cast<uchar>(payload.at(index + 1));
            codeUnits.append(static_cast<char16_t>(littleEndian ? first | (second << 8)
                                                                : (first << 8) | second));
        }
        for (qsizetype index = 0; index < codeUnits.size(); ++index) {
            const quint16 value = codeUnits.at(index);
            if (value >= 0xd800 && value <= 0xdbff) {
                if (index + 1 >= codeUnits.size() || codeUnits.at(index + 1) < 0xdc00 ||
                    codeUnits.at(index + 1) > 0xdfff) {
                    if (error) {
                        *error =
                            QStringLiteral("The UTF-16 file contains an invalid surrogate pair.");
                    }
                    return false;
                }
                ++index;
            } else if (value >= 0xdc00 && value <= 0xdfff) {
                if (error) {
                    *error = QStringLiteral("The UTF-16 file contains an invalid surrogate pair.");
                }
                return false;
            }
        }
        text = QString::fromUtf16(codeUnits.constData(), codeUnits.size());
        encoding = littleEndian ? TextEncoding::Utf16LittleEndian : TextEncoding::Utf16BigEndian;
    } else {
        bool validUtf8 = false;
        text = decodeUtf8(bytes, &validUtf8);
        if (!validUtf8) {
#ifdef Q_OS_WIN
            QString windows1256Text;
            QString windows1252Text;
            if (!decodeCodePage(bytes, 1256, &windows1256Text) ||
                !decodeCodePage(bytes, 1252, &windows1252Text)) {
                if (error) {
                    *error = QStringLiteral("The file encoding could not be detected.");
                }
                return false;
            }
            if (isArabicText(windows1256Text)) {
                text = std::move(windows1256Text);
                encoding = TextEncoding::Windows1256;
            } else {
                text = std::move(windows1252Text);
                encoding = TextEncoding::Windows1252;
            }
#else
            if (error) {
                *error = QStringLiteral("The file is not valid UTF-8 on this platform.");
            }
            return false;
#endif
        }
    }

    decoded->encoding = encoding;
    normalizeLineEndings(text, decoded);
    return true;
}

bool TextFileCodec::encode(const QString& text, TextEncoding encoding,
                           const QVector<LineEnding>& lineEndings, QByteArray* bytes,
                           QString* error) {
    if (!bytes) {
        if (error) {
            *error = QStringLiteral("No output buffer provided for encoded text.");
        }
        return false;
    }

    const LineEnding fallback = dominantLineEnding(lineEndings);
    QString serializedText;
    serializedText.reserve(text.size() + lineEndings.size());
    qsizetype newlineIndex = 0;
    for (QChar character : text) {
        if (character != QLatin1Char('\n')) {
            serializedText.append(character);
            continue;
        }
        const LineEnding ending =
            newlineIndex < lineEndings.size() ? lineEndings.at(newlineIndex) : fallback;
        ++newlineIndex;
        switch (ending) {
            case LineEnding::Lf:
                serializedText.append(QLatin1Char('\n'));
                break;
            case LineEnding::CrLf:
                serializedText.append(QStringLiteral("\r\n"));
                break;
            case LineEnding::Cr:
                serializedText.append(QLatin1Char('\r'));
                break;
        }
    }

    switch (encoding) {
        case TextEncoding::Utf8:
            *bytes = serializedText.toUtf8();
            return true;
        case TextEncoding::Utf8Bom:
            *bytes = QByteArray::fromHex("efbbbf") + serializedText.toUtf8();
            return true;
        case TextEncoding::Utf16LittleEndian:
        case TextEncoding::Utf16BigEndian: {
            bytes->clear();
            const bool littleEndian = encoding == TextEncoding::Utf16LittleEndian;
            bytes->reserve(2 + serializedText.size() * 2);
            bytes->append(littleEndian ? QByteArray::fromHex("fffe") : QByteArray::fromHex("feff"));
            const ushort* codeUnits = serializedText.utf16();
            for (qsizetype index = 0; index < serializedText.size(); ++index) {
                const quint16 value = codeUnits[index];
                bytes->append(static_cast<char>(littleEndian ? value & 0xff : value >> 8));
                bytes->append(static_cast<char>(littleEndian ? value >> 8 : value & 0xff));
            }
            return true;
        }
        case TextEncoding::Windows1256:
        case TextEncoding::Windows1252:
#ifdef Q_OS_WIN
            if (encodeCodePage(serializedText, encoding == TextEncoding::Windows1256 ? 1256 : 1252,
                               bytes)) {
                return true;
            }
            if (error) {
                *error = QStringLiteral("%1 cannot represent one or more characters in this file.")
                             .arg(encodingName(encoding));
            }
            return false;
#else
            if (error) {
                *error = QStringLiteral("%1 encoding is only supported on Windows.")
                             .arg(encodingName(encoding));
            }
            return false;
#endif
    }
    if (error) {
        *error = QStringLiteral("Unsupported text encoding.");
    }
    return false;
}

QString TextFileCodec::encodingName(TextEncoding encoding) {
    switch (encoding) {
        case TextEncoding::Utf8:
            return QStringLiteral("UTF-8");
        case TextEncoding::Utf8Bom:
            return QStringLiteral("UTF-8 BOM");
        case TextEncoding::Utf16LittleEndian:
            return QStringLiteral("UTF-16 LE");
        case TextEncoding::Utf16BigEndian:
            return QStringLiteral("UTF-16 BE");
        case TextEncoding::Windows1256:
            return QStringLiteral("Windows-1256");
        case TextEncoding::Windows1252:
            return QStringLiteral("Windows-1252");
    }
    return QStringLiteral("Unknown");
}

QString TextFileCodec::lineEndingName(LineEnding lineEnding) {
    switch (lineEnding) {
        case LineEnding::Lf:
            return QStringLiteral("LF");
        case LineEnding::CrLf:
            return QStringLiteral("CRLF");
        case LineEnding::Cr:
            return QStringLiteral("CR");
    }
    return QStringLiteral("Unknown");
}

}  // namespace forge::core
