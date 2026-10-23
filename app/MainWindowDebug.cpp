#include "DocumentCodec.h"
#include "EditorWidgets.h"
#include "MainWindow.h"
#include "TerminalView.h"
#include "WorkspaceSupport.h"

constexpr int kWindowLayoutVersion = 3;

void MainWindow::runCurrentPhpFile() {
    QPlainTextEdit* editor = currentEditor();
    if (!editor) {
        statusBar()->showMessage(QStringLiteral("Open a PHP file before running it."), 4000);
        return;
    }
    if (editor->document()->isModified()) {
        QMessageBox::information(this, QStringLiteral("Run PHP File"),
                                 QStringLiteral("Save the file before running it."));
        return;
    }

    const QString filePath = editor->property("filePath").toString();
    if (filePath.isEmpty() ||
        QFileInfo(filePath).suffix().compare(QStringLiteral("php"), Qt::CaseInsensitive) != 0) {
        QMessageBox::information(this, QStringLiteral("Run PHP File"),
                                 QStringLiteral("The current tab is not a saved PHP file."));
        return;
    }

    const QString phpExecutable = QStandardPaths::findExecutable(QStringLiteral("php.exe"));
    if (phpExecutable.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("PHP Interpreter Not Found"),
                             QStringLiteral("Add php.exe to PATH, then restart Forge IDE."));
        return;
    }

    if (auto* dock = findChild<QDockWidget*>(QStringLiteral("TerminalToolWindow"))) {
        dock->show();
        dock->raise();
    }
    runExternalCommand(phpExecutable, {filePath},
                       QStringLiteral("PHP: %1").arg(QFileInfo(filePath).fileName()));
}

void MainWindow::debugCurrentPhpFile() {
    QPlainTextEdit* editor = currentEditor();
    if (!editor || editor->document()->isModified()) {
        QMessageBox::information(this, QStringLiteral("Debug PHP File"),
                                 QStringLiteral("Open and save a PHP file before debugging."));
        return;
    }
    const QString filePath = editor->property("filePath").toString();
    if (filePath.isEmpty() ||
        QFileInfo(filePath).suffix().compare(QStringLiteral("php"), Qt::CaseInsensitive) != 0) {
        QMessageBox::information(this, QStringLiteral("Debug PHP File"),
                                 QStringLiteral("The current tab is not a saved PHP file."));
        return;
    }
    if (m_commandProcess && m_commandProcess->state() != QProcess::NotRunning) {
        statusBar()->showMessage(QStringLiteral("Wait for the current PHP process to finish."),
                                 4000);
        return;
    }

    const QString phpExecutable = QStandardPaths::findExecutable(QStringLiteral("php.exe"));
    if (phpExecutable.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("PHP Interpreter Not Found"),
                             QStringLiteral("Add php.exe to PATH, then restart Forge IDE."));
        return;
    }

    QProcess xdebugCheck;
    xdebugCheck.start(phpExecutable, {QStringLiteral("-r"),
                                      QStringLiteral("exit(extension_loaded('xdebug') ? 0 : 1);")});
    if (!xdebugCheck.waitForFinished(3000) || xdebugCheck.exitCode() != 0) {
        QMessageBox::information(
            this, QStringLiteral("Xdebug Setup"),
            QStringLiteral(
                "Xdebug is not loaded by this PHP interpreter.\n\n"
                "Install Xdebug for the same php.exe and ensure the extension is enabled. "
                "Forge will use it without modifying your global php.ini.\n\n"
                "After installation, try Debug again. Xdebug 3 will connect to "
                "127.0.0.1:9003."));
        return;
    }

    if (!m_debugServer) {
        m_debugServer = new QTcpServer(this);
        connect(m_debugServer, &QTcpServer::newConnection, this, [this]() {
            while (m_debugServer->hasPendingConnections()) {
                QTcpSocket* socket = m_debugServer->nextPendingConnection();
                if (!socket) {
                    continue;
                }
                if (m_debugSocket && m_debugSocket->state() != QAbstractSocket::UnconnectedState) {
                    socket->disconnectFromHost();
                    socket->deleteLater();
                    continue;
                }

                m_debugSocket = socket;
                m_debugBuffer.clear();
                m_debugTransactionId = 1;
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                    m_debugBuffer.append(socket->readAll());
                    int terminator = -1;
                    while ((terminator = m_debugBuffer.indexOf('\0')) >= 0) {
                        const QByteArray packet = m_debugBuffer.left(terminator);
                        m_debugBuffer.remove(0, terminator + 1);
                        const QString message = QString::fromUtf8(packet);
                        if (message.startsWith(QStringLiteral("<init"))) {
                            appendTerminalOutput(QStringLiteral("Xdebug session connected.\r\n"));
                            sendNextDebuggerCommand();
                            continue;
                        }

                        const QRegularExpression statusExpression(
                            QStringLiteral("\\bstatus=\"([^\"]+)\""));
                        const auto statusMatch = statusExpression.match(message);
                        if (statusMatch.hasMatch()) {
                            const QString state = statusMatch.captured(1);
                            appendTerminalOutput(
                                QStringLiteral("Xdebug status: %1\r\n").arg(state));
                            if (state == QStringLiteral("break")) {
                                statusBar()->showMessage(
                                    QStringLiteral(
                                        "Debugger paused — use Run → Step Over/Into/Out."),
                                    6000);
                            } else if (state == QStringLiteral("stopped")) {
                                m_debugCommands.clear();
                                if (m_debugServer) {
                                    m_debugServer->close();
                                }
                                statusBar()->showMessage(QStringLiteral("Debug session finished."),
                                                         4000);
                            }
                        }

                        if (!m_debugCommands.isEmpty()) {
                            sendNextDebuggerCommand();
                        }
                    }
                });
                connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
                    appendTerminalOutput(QStringLiteral("Xdebug session disconnected.\r\n"));
                    if (m_debugSocket == socket) {
                        m_debugSocket = nullptr;
                        m_debugBuffer.clear();
                        m_debugCommands.clear();
                    }
                    socket->deleteLater();
                });
            }
        });
    }

    if (!m_debugServer->listen(QHostAddress::LocalHost, 9003)) {
        QMessageBox::warning(this, QStringLiteral("Xdebug Listener"),
                             QStringLiteral("Could not listen on 127.0.0.1:9003:\n%1")
                                 .arg(m_debugServer->errorString()));
        return;
    }

    const QUrl fileUrl = QUrl::fromLocalFile(filePath);
    const QString encodedFileUrl = QString::fromUtf8(fileUrl.toEncoded(QUrl::FullyEncoded));
    const int line = editor->textCursor().blockNumber() + 1;
    m_debugCommands = {
        QStringLiteral("feature_set -n max_depth -v 5"),
        QStringLiteral("breakpoint_set -t line -f %1 -n %2").arg(encodedFileUrl).arg(line),
        QStringLiteral("run")};

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("XDEBUG_MODE"), QStringLiteral("debug"));
    environment.insert(QStringLiteral("XDEBUG_CONFIG"),
                       QStringLiteral("client_host=127.0.0.1 client_port=9003 log_level=0"));
    QStringList arguments{QStringLiteral("-d"), QStringLiteral("xdebug.start_with_request=yes"),
                          filePath};
    appendTerminalOutput(QStringLiteral("\r\n--- Xdebug: %1 (127.0.0.1:9003) ---\r\n")
                             .arg(QFileInfo(filePath).fileName()));
    if (auto* dock = findChild<QDockWidget*>(QStringLiteral("TerminalToolWindow"))) {
        dock->show();
        dock->raise();
    }
    runExternalCommand(phpExecutable, arguments,
                       QStringLiteral("Xdebug: %1").arg(QFileInfo(filePath).fileName()),
                       &environment);
    statusBar()->showMessage(QStringLiteral("Waiting for Xdebug on 127.0.0.1:9003..."), 5000);
}

void MainWindow::sendNextDebuggerCommand() {
    if (!m_debugSocket || m_debugSocket->state() != QAbstractSocket::ConnectedState ||
        m_debugCommands.isEmpty()) {
        return;
    }
    const QString command = m_debugCommands.takeFirst();
    const QByteArray packet =
        QStringLiteral("%1 -i %2").arg(command).arg(m_debugTransactionId++).toUtf8() + '\0';
    if (m_debugSocket->write(packet) < 0) {
        appendTerminalOutput(QStringLiteral("Could not send Xdebug command: %1\r\n")
                                 .arg(m_debugSocket->errorString()));
    }
}

void MainWindow::sendDebuggerStep(const QString& command) {
    if (!m_debugSocket || m_debugSocket->state() != QAbstractSocket::ConnectedState) {
        statusBar()->showMessage(QStringLiteral("No active Xdebug session."), 4000);
        return;
    }
    m_debugCommands.prepend(command);
    sendNextDebuggerCommand();
}

void MainWindow::profileCurrentPhpFile() {
    QPlainTextEdit* editor = currentEditor();
    if (!editor || editor->document()->isModified()) {
        QMessageBox::information(this, QStringLiteral("Profile PHP File"),
                                 QStringLiteral("Open and save a PHP file before profiling."));
        return;
    }
    const QString filePath = editor->property("filePath").toString();
    if (filePath.isEmpty() ||
        QFileInfo(filePath).suffix().compare(QStringLiteral("php"), Qt::CaseInsensitive) != 0) {
        QMessageBox::information(this, QStringLiteral("Profile PHP File"),
                                 QStringLiteral("The current tab is not a saved PHP file."));
        return;
    }
    if (m_commandProcess && m_commandProcess->state() != QProcess::NotRunning) {
        statusBar()->showMessage(QStringLiteral("Wait for the current PHP process to finish."),
                                 4000);
        return;
    }
    const QString phpExecutable = QStandardPaths::findExecutable(QStringLiteral("php.exe"));
    if (phpExecutable.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("PHP Interpreter Not Found"),
                             QStringLiteral("Add php.exe to PATH, then restart Forge IDE."));
        return;
    }

    QProcess xdebugCheck;
    xdebugCheck.start(phpExecutable, {QStringLiteral("-r"),
                                      QStringLiteral("exit(extension_loaded('xdebug') ? 0 : 1);")});
    if (!xdebugCheck.waitForFinished(3000) || xdebugCheck.exitCode() != 0) {
        QMessageBox::information(
            this, QStringLiteral("Xdebug Setup"),
            QStringLiteral("Xdebug is not loaded by this PHP interpreter. Install and enable "
                           "Xdebug 3, then try profiling again."));
        return;
    }

    m_profileDirectory =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
            .filePath(QStringLiteral("profiles"));
    if (!QDir().mkpath(m_profileDirectory)) {
        QMessageBox::warning(this, QStringLiteral("Profiler"),
                             QStringLiteral("Could not create profile output directory:\n%1")
                                 .arg(m_profileDirectory));
        return;
    }
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("XDEBUG_MODE"), QStringLiteral("profile"));
    const QString outputName = QStringLiteral("cachegrind.out.%p.%t");
    QStringList arguments{QStringLiteral("-d"),
                          QStringLiteral("xdebug.start_with_request=yes"),
                          QStringLiteral("-d"),
                          QStringLiteral("xdebug.output_dir=%1").arg(m_profileDirectory),
                          QStringLiteral("-d"),
                          QStringLiteral("xdebug.profiler_output_name=%1").arg(outputName),
                          filePath};
    m_profileAfterRun = true;
    m_profileStartedAt = QDateTime::currentMSecsSinceEpoch();
    if (auto* dock = findChild<QDockWidget*>(QStringLiteral("TerminalToolWindow"))) {
        dock->show();
        dock->raise();
    }
    runExternalCommand(phpExecutable, arguments,
                       QStringLiteral("Xdebug profile: %1").arg(QFileInfo(filePath).fileName()),
                       &environment);
}
