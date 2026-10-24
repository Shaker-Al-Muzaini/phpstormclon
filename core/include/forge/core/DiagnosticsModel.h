#pragma once

#include <QObject>
#include <QString>
#include <QVector>

namespace forge::core {

enum class DiagnosticSeverity {
    Error,
    Warning,
    Information,
};

struct DiagnosticFrame {
    QString file;
    int line = 1;
    QString label;
};

struct Diagnostic {
    QString file;
    int line = 1;
    int col = 1;
    int endLine = 1;
    int endCol = 2;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    QString message;
    QString source;
    QString code;
    QVector<DiagnosticFrame> frames;
    QString ownerFile;
};

class DiagnosticsModel final : public QObject {
    Q_OBJECT
public:
    explicit DiagnosticsModel(QObject* parent = nullptr);

    QVector<Diagnostic> diagnostics() const;
    QVector<Diagnostic> diagnosticsForFile(const QString& file) const;
    void setFileDiagnostics(const QString& file, const QString& source,
                            const QVector<Diagnostic>& diagnostics);
    void clearFileSource(const QString& file, const QString& source);
    void clearSource(const QString& source);
    QString statusMessage() const;
    void setStatusMessage(const QString& message);

    static QVector<Diagnostic> parsePhpLintOutput(const QString& file, const QString& output,
                                                  int exitCode);
    static QVector<Diagnostic> parseLaravelLog(const QString& logFile, const QByteArray& contents,
                                               const QString& projectRoot);

signals:
    void fileDiagnosticsChanged(const QString& file);
    void diagnosticsChanged();
    void statusMessageChanged(const QString& message);

private:
    void emitChanges(const QString& file);

    QVector<Diagnostic> m_diagnostics;
    QString m_statusMessage;
};

}  // namespace forge::core
