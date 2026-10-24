#include "forge/core/DiagnosticsModel.h"

#include <algorithm>
#include <utility>

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace forge::core {

namespace {
constexpr qsizetype kMaxLaravelLogBytes = 2 * 1024 * 1024;

QString absolutePath(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool pathIsWithin(const QString& path, const QString& root) {
    const QString cleanPath = QDir::fromNativeSeparators(
        QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    QString cleanRoot = QDir::fromNativeSeparators(
        QDir::cleanPath(QFileInfo(root).absoluteFilePath()));
    if (!cleanRoot.endsWith(QLatin1Char('/'))) {
        cleanRoot.append(QLatin1Char('/'));
    }
#ifdef Q_OS_WIN
    return cleanPath.startsWith(cleanRoot, Qt::CaseInsensitive);
#else
    return cleanPath.startsWith(cleanRoot);
#endif
}

QVector<DiagnosticFrame> parseStackFrames(const QStringList& lines, const QString& projectRoot) {
    static const QRegularExpression frameExpression(
        QStringLiteral("^\\s*#\\d+\\s+(.+?)(?:\\((\\d+)\\)|:(\\d+))(?::|$)"));
    QVector<DiagnosticFrame> frames;
    for (const QString& line : lines) {
        const auto match = frameExpression.match(line);
        if (!match.hasMatch()) {
            continue;
        }
        QString path = match.captured(1).trimmed();
        if (path.isEmpty() || path.startsWith(QStringLiteral("internal:"))) {
            continue;
        }
        if (QDir::isRelativePath(path)) {
            path = QDir(projectRoot).absoluteFilePath(path);
        }
        path = absolutePath(path);
        if (!pathIsWithin(path, projectRoot)) {
            continue;
        }
        bool lineOk = false;
        const int frameLine = match.captured(2).isEmpty() ? match.captured(3).toInt(&lineOk)
                                                          : match.captured(2).toInt(&lineOk);
        if (!lineOk || frameLine < 1) {
            continue;
        }
        frames.append({path, frameLine, line.trimmed()});
    }
    return frames;
}
}  // namespace

DiagnosticsModel::DiagnosticsModel(QObject* parent) : QObject(parent) {}

QVector<Diagnostic> DiagnosticsModel::diagnostics() const {
    return m_diagnostics;
}

QVector<Diagnostic> DiagnosticsModel::diagnosticsForFile(const QString& file) const {
    QVector<Diagnostic> result;
    const QString normalized = QDir::cleanPath(QFileInfo(file).absoluteFilePath());
    for (const Diagnostic& diagnostic : m_diagnostics) {
#ifdef Q_OS_WIN
        const bool matches = diagnostic.file.compare(normalized, Qt::CaseInsensitive) == 0;
#else
        const bool matches = diagnostic.file == normalized;
#endif
        if (matches) {
            result.append(diagnostic);
        }
    }
    return result;
}

void DiagnosticsModel::setFileDiagnostics(const QString& file, const QString& source,
                                          const QVector<Diagnostic>& diagnostics) {
    const QString normalized = absolutePath(file);
    m_diagnostics.erase(
        std::remove_if(m_diagnostics.begin(), m_diagnostics.end(),
                       [&normalized, &source](const Diagnostic& diagnostic) {
#ifdef Q_OS_WIN
                           const bool sameFile =
                               diagnostic.ownerFile.compare(normalized, Qt::CaseInsensitive) == 0;
#else
                           const bool sameFile = diagnostic.ownerFile == normalized;
#endif
                           return sameFile && diagnostic.source == source;
                       }),
        m_diagnostics.end());
    for (Diagnostic diagnostic : diagnostics) {
        if (diagnostic.file.isEmpty()) {
            diagnostic.file = normalized;
        } else {
            diagnostic.file = absolutePath(diagnostic.file);
        }
        diagnostic.ownerFile = normalized;
        diagnostic.source = source;
        m_diagnostics.append(std::move(diagnostic));
    }
    emitChanges(normalized);
}

void DiagnosticsModel::clearFileSource(const QString& file, const QString& source) {
    setFileDiagnostics(file, source, {});
}

void DiagnosticsModel::clearSource(const QString& source) {
    QSet<QString> changedFiles;
    for (const Diagnostic& diagnostic : std::as_const(m_diagnostics)) {
        if (diagnostic.source == source) {
            changedFiles.insert(diagnostic.file);
        }
    }
    if (changedFiles.isEmpty()) {
        return;
    }
    m_diagnostics.erase(
        std::remove_if(m_diagnostics.begin(), m_diagnostics.end(),
                       [&source](const Diagnostic& diagnostic) {
                           return diagnostic.source == source;
                       }),
        m_diagnostics.end());
    for (const QString& file : std::as_const(changedFiles)) {
        emit fileDiagnosticsChanged(file);
    }
    emit diagnosticsChanged();
}

QString DiagnosticsModel::statusMessage() const {
    return m_statusMessage;
}

void DiagnosticsModel::setStatusMessage(const QString& message) {
    if (m_statusMessage == message) {
        return;
    }
    m_statusMessage = message;
    emit statusMessageChanged(message);
}

QVector<Diagnostic> DiagnosticsModel::parsePhpLintOutput(const QString& file,
                                                         const QString& output, int exitCode) {
    if (exitCode == 0) {
        return {};
    }

    const QString normalizedOutput = output.trimmed();
    static const QRegularExpression expression(
        QStringLiteral("(?:PHP\\s+)?(Parse|Fatal) error:\\s*(.*?)\\s+in\\s+(.+?)\\s+on line\\s+(\\d+)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = expression.match(normalizedOutput);
    if (match.hasMatch()) {
        bool lineOk = false;
        const int line = match.captured(4).toInt(&lineOk);
        if (lineOk && line > 0) {
            const QString reportedFile = match.captured(3).trimmed();
            return {{reportedFile.isEmpty() ? absolutePath(file) : absolutePath(reportedFile),
                     line,
                     1,
                     line,
                     2,
                     DiagnosticSeverity::Error,
                     match.captured(2).trimmed(),
                     QStringLiteral("php -l"),
                     match.captured(1).toLower() + QStringLiteral(".syntax"),
                     {}}};
        }
    }

    return {{absolutePath(file),
             1,
             1,
             1,
             2,
             DiagnosticSeverity::Error,
             normalizedOutput.isEmpty() ? QStringLiteral("PHP syntax check failed.")
                                        : normalizedOutput,
             QStringLiteral("php -l"),
             QStringLiteral("php.syntax"),
             {}}};
}

QVector<Diagnostic> DiagnosticsModel::parseLaravelLog(const QString& logFile,
                                                      const QByteArray& contents,
                                                      const QString& projectRoot) {
    if (contents.isEmpty()) {
        return {};
    }
    const QByteArray bounded =
        contents.size() > kMaxLaravelLogBytes ? contents.right(kMaxLaravelLogBytes) : contents;
    const QString text = QString::fromUtf8(bounded);
    const QStringList lines = text.split(QLatin1Char('\n'));
    static const QRegularExpression entryExpression(
        QStringLiteral("^\\[[^\\]]+\\]\\s+[^\\s]+\\.(?:ERROR|CRITICAL|ALERT|EMERGENCY):\\s*(.*)$"),
        QRegularExpression::CaseInsensitiveOption);

    int entryStart = -1;
    for (int index = lines.size() - 1; index >= 0; --index) {
        if (entryExpression.match(lines.at(index).trimmed()).hasMatch()) {
            entryStart = index;
            break;
        }
    }
    if (entryStart < 0) {
        return {};
    }

    const QStringList latestEntry = lines.mid(entryStart);
    const auto headerMatch = entryExpression.match(lines.at(entryStart).trimmed());
    const QString headerMessage = headerMatch.captured(1).trimmed();
    const QString block = latestEntry.join(QLatin1Char('\n'));
    static const QRegularExpression exceptionExpression(
        QStringLiteral("([A-Za-z_\\\\][A-Za-z0-9_\\\\]*(?:Exception|Error))"
                       "(?:\\s*\\(code:\\s*[^)]+\\))?:\\s*(.+?)\\s+at\\s+(.+?):(\\d+)"));
    const auto exceptionMatch = exceptionExpression.match(block);
    const QVector<DiagnosticFrame> frames = parseStackFrames(latestEntry, projectRoot);

    QString message = headerMessage;
    QString diagnosticFile = absolutePath(logFile);
    int diagnosticLine = entryStart + 1;
    QString code = QStringLiteral("laravel.runtime");
    if (exceptionMatch.hasMatch()) {
        message = exceptionMatch.captured(1) + QStringLiteral(": ") +
                  exceptionMatch.captured(2).trimmed();
        diagnosticFile = exceptionMatch.captured(3).trimmed();
        if (QDir::isRelativePath(diagnosticFile)) {
            diagnosticFile = QDir(projectRoot).absoluteFilePath(diagnosticFile);
        }
        bool lineOk = false;
        const int line = exceptionMatch.captured(4).toInt(&lineOk);
        if (lineOk && line > 0) {
            diagnosticLine = line;
        }
        if (!pathIsWithin(diagnosticFile, projectRoot)) {
            diagnosticFile = absolutePath(logFile);
            diagnosticLine = entryStart + 1;
        } else {
            diagnosticFile = absolutePath(diagnosticFile);
        }
        code = exceptionMatch.captured(1);
    }
    if (message.isEmpty()) {
        message = QStringLiteral("Laravel runtime error");
    }
    if (!frames.isEmpty() && diagnosticFile == absolutePath(logFile)) {
        diagnosticFile = frames.constFirst().file;
        diagnosticLine = frames.constFirst().line;
    }

    return {{diagnosticFile,
             diagnosticLine,
             1,
             diagnosticLine,
             2,
             DiagnosticSeverity::Error,
             message,
             QStringLiteral("laravel.log"),
             code,
             frames}};
}

void DiagnosticsModel::emitChanges(const QString& file) {
    emit fileDiagnosticsChanged(file);
    emit diagnosticsChanged();
}

}  // namespace forge::core
