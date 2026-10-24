#include <QFile>
#include <QFileInfo>
#include <QHelpEvent>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include "EditorWidgets.h"
#include "MainWindow.h"
#include "ProblemsPanel.h"

namespace {
QString localDocumentUri(const QString& path) {
    return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
}

QString phpExecutableName() {
#ifdef Q_OS_WIN
    return QStringLiteral("php.exe");
#else
    return QStringLiteral("php");
#endif
}

}  // namespace

void MainWindow::updateDiagnostics() {
    for (auto* editor : findChildren<QPlainTextEdit*>()) {
        if (!editor->property("forgeSourceEditor").toBool()) {
            continue;
        }
        static_cast<ForgeCodeEditor*>(editor)->setDiagnostics(
            m_diagnostics.diagnosticsForFile(editor->property("filePath").toString()));
    }

    int errors = 0;
    int warnings = 0;
    for (const auto& diagnostic : m_diagnostics.diagnostics()) {
        errors += diagnostic.severity == forge::core::DiagnosticSeverity::Error;
        warnings += diagnostic.severity == forge::core::DiagnosticSeverity::Warning;
    }
    if (m_problemCountButton) {
        m_problemCountButton->setText(QStringLiteral("✖ %1  ⚠ %2").arg(errors).arg(warnings));
        m_problemCountButton->setToolTip(
            QStringLiteral("%1 errors, %2 warnings — open Problems").arg(errors).arg(warnings));
    }
    updateProblemsCurrentDocument();
}

void MainWindow::updateProblemsCurrentDocument() {
    if (!m_problemsPanel) {
        return;
    }
    const auto* editor = currentEditor();
    m_problemsPanel->setCurrentDocument(
        editor ? editor->property("filePath").toString() : QString(),
        editor ? editor->toPlainText() : QString());
}

void MainWindow::connectPhpIntelDocument(QPlainTextEdit* editor) {
    if (!editor) {
        return;
    }
    const QString path = editor->property("filePath").toString();
    if (path.isEmpty() || QFileInfo(path).suffix().compare(QStringLiteral("php"),
                                                           Qt::CaseInsensitive) != 0 ||
        path.endsWith(QStringLiteral(".blade.php"), Qt::CaseInsensitive)) {
        return;
    }
    if (!editor->property("phpIntelChangeConnected").toBool()) {
        auto* timer = new QTimer(editor);
        timer->setObjectName(QStringLiteral("phpIntelChangeDebounce"));
        timer->setSingleShot(true);
        timer->setInterval(75);
        connect(timer, &QTimer::timeout, this, [this, editor]() { sendPhpIntelDidChange(editor); });
        connect(editor->document(), &QTextDocument::contentsChanged, timer,
                qOverload<>(&QTimer::start));
        editor->setProperty("phpIntelChangeConnected", true);
    }
    sendPhpIntelDidOpen(editor);
}

void MainWindow::sendPhpIntelDidOpen(QPlainTextEdit* editor) {
    if (!editor || !m_phpIntelClient) {
        return;
    }
    QProcess* process = m_supervisor->process(QStringLiteral("phpintel"));
    if (!process || process->state() != QProcess::Running) {
        return;
    }
    const QString path = editor->property("filePath").toString();
    if (path.isEmpty() || QFileInfo(path).suffix().compare(QStringLiteral("php"),
                                                           Qt::CaseInsensitive) != 0 ||
        path.endsWith(QStringLiteral(".blade.php"), Qt::CaseInsensitive)) {
        return;
    }
    const int version = 1;
    editor->setProperty("phpIntelVersion", version);
    editor->setProperty("phpIntelOpened", true);
    const QJsonObject textDocument{{QStringLiteral("uri"), localDocumentUri(path)},
                                   {QStringLiteral("languageId"), QStringLiteral("php")},
                                   {QStringLiteral("version"), version},
                                   {QStringLiteral("text"), editor->toPlainText()}};
    m_phpIntelClient->sendNotification(
        QStringLiteral("textDocument/didOpen"),
        {{QStringLiteral("textDocument"), textDocument}});
}

void MainWindow::sendPhpIntelDidChange(QPlainTextEdit* editor) {
    if (!editor || !m_phpIntelClient) {
        return;
    }
    QProcess* process = m_supervisor->process(QStringLiteral("phpintel"));
    if (!process || process->state() != QProcess::Running) {
        return;
    }
    if (!editor->property("phpIntelOpened").toBool()) {
        sendPhpIntelDidOpen(editor);
        return;
    }
    const QString path = editor->property("filePath").toString();
    bool versionOk = false;
    int version = editor->property("phpIntelVersion").toInt(&versionOk);
    if (!versionOk) {
        version = 1;
    }
    ++version;
    editor->setProperty("phpIntelVersion", version);
    const QJsonObject textDocument{{QStringLiteral("uri"), localDocumentUri(path)},
                                   {QStringLiteral("version"), version}};
    const QJsonArray contentChanges{
        QJsonObject{{QStringLiteral("text"), editor->toPlainText()}}};
    m_phpIntelClient->sendNotification(
        QStringLiteral("textDocument/didChange"),
        {{QStringLiteral("textDocument"), textDocument},
         {QStringLiteral("contentChanges"), contentChanges}});
}

void MainWindow::handlePhpIntelDiagnostics(const QJsonObject& params) {
    const QString uri = params.value(QStringLiteral("uri")).toString();
    const QString file = QUrl(uri).toLocalFile();
    if (file.isEmpty()) {
        qWarning() << "[MainWindow] phpintel published diagnostics without a local file URI.";
        return;
    }
    QVector<forge::core::Diagnostic> diagnostics;
    for (const auto& value : params.value(QStringLiteral("diagnostics")).toArray()) {
        const QJsonObject item = value.toObject();
        const QJsonObject range = item.value(QStringLiteral("range")).toObject();
        const QJsonObject start = range.value(QStringLiteral("start")).toObject();
        const QJsonObject end = range.value(QStringLiteral("end")).toObject();
        const int severity = item.value(QStringLiteral("severity")).toInt(1);
        diagnostics.append(
            {file,
             start.value(QStringLiteral("line")).toInt() + 1,
             start.value(QStringLiteral("character")).toInt() + 1,
             end.value(QStringLiteral("line")).toInt() + 1,
             end.value(QStringLiteral("character")).toInt() + 1,
             severity == 2 ? forge::core::DiagnosticSeverity::Warning
                           : severity == 3 ? forge::core::DiagnosticSeverity::Information
                                           : forge::core::DiagnosticSeverity::Error,
             item.value(QStringLiteral("message")).toString(),
             QStringLiteral("phpintel"),
             item.value(QStringLiteral("code")).toString(),
             {}});
    }
    m_diagnostics.setFileDiagnostics(file, QStringLiteral("phpintel"), diagnostics);
    if (!diagnostics.isEmpty()) {
        m_diagnostics.clearFileSource(file, QStringLiteral("php -l"));
    }
}

void MainWindow::runPhpSyntaxCheck(const QString& path) {
    if (!m_phpLintEnabled || path.isEmpty() ||
        QFileInfo(path).suffix().compare(QStringLiteral("php"), Qt::CaseInsensitive) != 0 ||
        path.endsWith(QStringLiteral(".blade.php"), Qt::CaseInsensitive)) {
        return;
    }
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    m_diagnostics.clearFileSource(absolutePath, QStringLiteral("php -l"));
    const QString executable = QStandardPaths::findExecutable(phpExecutableName());
    if (executable.isEmpty()) {
        m_diagnostics.setStatusMessage(
            QStringLiteral("PHP was not found. Install PHP or add %1 to PATH to run the optional "
                           "syntax check.")
                .arg(phpExecutableName()));
        if (auto* dock = findChild<QDockWidget*>(QStringLiteral("ProblemsToolWindow"))) {
            dock->show();
            dock->raise();
        }
        return;
    }
    if (m_diagnostics.statusMessage().startsWith(QStringLiteral("PHP was not found."))) {
        m_diagnostics.setStatusMessage(QString());
    }

    if (QProcess* previous = m_phpLintProcesses.value(absolutePath)) {
        previous->kill();
        previous->deleteLater();
    }
    auto* process = new QProcess(this);
    process->setProperty("phpLintPath", absolutePath);
    m_phpLintProcesses.insert(absolutePath, process);
    auto* timeout = new QTimer(process);
    timeout->setSingleShot(true);
    timeout->setInterval(5000);
    connect(process, &QProcess::started, timeout, qOverload<>(&QTimer::start));
    connect(timeout, &QTimer::timeout, this, [this, process, absolutePath]() {
        if (m_phpLintProcesses.value(absolutePath) != process) {
            return;
        }
        process->setProperty("phpLintTimedOut", true);
        for (const auto& diagnostic : m_diagnostics.diagnosticsForFile(absolutePath)) {
            if (diagnostic.source == QStringLiteral("phpintel")) {
                process->kill();
                return;
            }
        }
        forge::core::Diagnostic diagnostic{
            absolutePath,
            1,
            1,
            1,
            2,
            forge::core::DiagnosticSeverity::Warning,
            QStringLiteral("PHP syntax check timed out after 5 seconds."),
            QStringLiteral("php -l"),
            QStringLiteral("php.timeout"),
            {}};
        m_diagnostics.setFileDiagnostics(absolutePath, QStringLiteral("php -l"), {diagnostic});
        process->kill();
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, timeout, absolutePath](int exitCode, QProcess::ExitStatus) {
                timeout->stop();
                if (m_phpLintProcesses.value(absolutePath) != process) {
                    process->deleteLater();
                    return;
                }
                m_phpLintProcesses.remove(absolutePath);
                if (!process->property("phpLintTimedOut").toBool()) {
                    const QString output =
                        QString::fromLocal8Bit(process->readAllStandardOutput() +
                                               process->readAllStandardError());
                    const auto diagnostics = forge::core::DiagnosticsModel::parsePhpLintOutput(
                        absolutePath, output, exitCode);
                    bool hasPhpIntelDiagnostics = false;
                    for (const auto& diagnostic :
                         m_diagnostics.diagnosticsForFile(absolutePath)) {
                        hasPhpIntelDiagnostics |=
                            diagnostic.source == QStringLiteral("phpintel");
                    }
                    if (!hasPhpIntelDiagnostics) {
                        m_diagnostics.setFileDiagnostics(absolutePath, QStringLiteral("php -l"),
                                                         diagnostics);
                    }
                }
                process->deleteLater();
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, absolutePath](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart ||
                    m_phpLintProcesses.value(absolutePath) != process) {
                    return;
                }
                m_phpLintProcesses.remove(absolutePath);
                m_diagnostics.setStatusMessage(
                    QStringLiteral("Could not start PHP syntax check: %1")
                        .arg(process->errorString()));
                process->deleteLater();
            });
    process->start(executable, {QStringLiteral("-l"), absolutePath});
}

void MainWindow::updateLaravelLogDiagnostics() {
    if (m_laravelLogPath.isEmpty()) {
        return;
    }
    QFile log(m_laravelLogPath);
    if (!log.exists()) {
        m_diagnostics.clearFileSource(m_laravelLogPath, QStringLiteral("laravel.log"));
        return;
    }
    if (!log.open(QIODevice::ReadOnly)) {
        qWarning() << "[MainWindow] Cannot read Laravel log:" << m_laravelLogPath
                   << log.errorString();
        return;
    }
    constexpr qint64 maxBytes = 2 * 1024 * 1024;
    if (log.size() > maxBytes && !log.seek(log.size() - maxBytes)) {
        qWarning() << "[MainWindow] Cannot seek to the latest Laravel log entries:"
                   << m_laravelLogPath;
        return;
    }
    const auto diagnostics = forge::core::DiagnosticsModel::parseLaravelLog(
        m_laravelLogPath, log.readAll(), m_projectRoot);
    m_diagnostics.setFileDiagnostics(m_laravelLogPath, QStringLiteral("laravel.log"), diagnostics);
}
