#include <QStringDecoder>
#include <QTimer>

#include "DocumentCodec.h"
#include "EditorWidgets.h"
#include "GitPanel.h"
#include "MainWindow.h"
#include "TerminalView.h"
#include "WorkspaceSupport.h"

constexpr int kWindowLayoutVersion = 3;

QString cachegrindTopFunctions(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QStringLiteral("Could not read profiler output %1: %2")
            .arg(path, file.errorString());
    }

    QTextStream stream(&file);
    QStringList events;
    QString currentFunction;
    QHash<QString, quint64> selfCosts;
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.startsWith(QStringLiteral("events:"))) {
            events =
                line.mid(7).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        } else if (line.startsWith(QStringLiteral("fn="))) {
            currentFunction = line.mid(3);
        } else if (!currentFunction.isEmpty() && !line.isEmpty() && line.front().isDigit()) {
            const QStringList columns =
                line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            if (columns.size() > events.size()) {
                bool ok = false;
                const quint64 cost = columns.at(1).toULongLong(&ok);
                if (ok) {
                    selfCosts[currentFunction] += cost;
                }
            }
        }
    }

    if (events.isEmpty() || selfCosts.isEmpty()) {
        return QStringLiteral("Profiler output could not be parsed: %1").arg(path);
    }

    QList<QPair<QString, quint64>> ranked;
    for (auto it = selfCosts.cbegin(); it != selfCosts.cend(); ++it) {
        ranked.append(qMakePair(it.key(), it.value()));
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const auto& left, const auto& right) { return left.second > right.second; });

    QStringList lines{
        QStringLiteral("Xdebug profile (self cost: %1)").arg(events.constFirst()),
        QStringLiteral("Largest self-cost totals; these are profiler event counts, not wall-clock "
                       "percentages. Source: %1")
            .arg(path)};
    const int limit = qMin(15, static_cast<int>(ranked.size()));
    for (int index = 0; index < limit; ++index) {
        lines.append(QStringLiteral("%1. %2 — %3")
                         .arg(index + 1)
                         .arg(ranked.at(index).first)
                         .arg(ranked.at(index).second));
    }
    return lines.join(QLatin1Char('\n'));
}

void MainWindow::createTerminalSession() {
    if (!m_terminalTabs) {
        return;
    }

    auto* output = new TerminalOutputView(m_terminalTabs);
    output->setSharedHistory(m_terminalHistory);
    output->setObjectName(QStringLiteral("TerminalOutput"));
    output->setMaximumBlockCount(5000);
    output->setLineWrapMode(QPlainTextEdit::NoWrap);
    auto* process = new QProcess(this);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    m_terminalViews.append(output);
    m_terminalProcesses.append(process);

    output->setCommandHandler(
        [this, output](const QString& command) { submitTerminalCommand(command, output); });
    auto stdoutDecoder = std::make_shared<QStringDecoder>(QStringDecoder::Utf8);
    auto stderrDecoder = std::make_shared<QStringDecoder>(QStringDecoder::Utf8);
    connect(process, &QProcess::readyReadStandardOutput, this,
            [this, process, output, stdoutDecoder]() {
                appendTerminalOutput((*stdoutDecoder)(process->readAllStandardOutput()), output);
            });
    connect(process, &QProcess::readyReadStandardError, this,
            [this, process, output, stderrDecoder]() {
                appendTerminalOutput((*stderrDecoder)(process->readAllStandardError()), output);
            });
    connect(process, &QProcess::started, this, [this, process, output]() {
        appendTerminalOutput(QStringLiteral("Terminal session started.\r\n"), output);
        const QStringList pendingCommands = m_pendingTerminalCommands.take(process);
        for (const QString& command : pendingCommands) {
            process->write(command.toUtf8() + "\n");
        }
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, output](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    appendTerminalOutput(QStringLiteral("Could not start PowerShell: %1\r\n")
                                             .arg(process->errorString()),
                                         output);
                    m_pendingTerminalCommands.remove(process);
                }
            });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, output](int exitCode, QProcess::ExitStatus status) {
                appendTerminalOutput(
                    QStringLiteral("\r\nPowerShell exited (code %1, status %2).\r\n")
                        .arg(exitCode)
                        .arg(static_cast<int>(status)),
                    output);
                m_pendingTerminalCommands.remove(process);
            });

    const int tabIndex =
        m_terminalTabs->addTab(output, QStringLiteral("Local %1").arg(m_terminalTabs->count() + 1));
    auto* closeButton = new QToolButton(m_terminalTabs->tabBar());
    closeButton->setIcon(forgeIcon(QStringLiteral("close"), QColor(QStringLiteral("#8C8FA3")),
                                   QColor(QStringLiteral("#DFE1E5"))));
    closeButton->setIconSize(QSize(12, 12));
    closeButton->setFixedSize(20, 20);
    closeButton->setAutoRaise(true);
    closeButton->setToolTip(QStringLiteral("Close terminal"));
    closeButton->setStyleSheet(
        QStringLiteral("QToolButton { border: 0; border-radius: 3px; padding: 2px; }"
                       "QToolButton:hover { background: %1; }")
            .arg(m_themeName.compare(QStringLiteral("Daylight"), Qt::CaseInsensitive) == 0
                     ? QStringLiteral("#E1E5EC")
                     : QStringLiteral("#393B40")));
    m_terminalTabs->tabBar()->setTabButton(tabIndex, QTabBar::RightSide, closeButton);
    connect(closeButton, &QToolButton::clicked, this, [this, closeButton]() {
        for (int index = 0; index < m_terminalTabs->count(); ++index) {
            if (m_terminalTabs->tabBar()->tabButton(index, QTabBar::RightSide) == closeButton) {
                closeTerminalSession(index);
                return;
            }
        }
    });
    m_terminalTabs->setCurrentWidget(output);
    startTerminal(process, output);
    output->setFocus();
}

void MainWindow::closeTerminalSession(int index) {
    if (index < 0 || index >= m_terminalViews.size() || index >= m_terminalProcesses.size()) {
        return;
    }

    QPlainTextEdit* output = m_terminalViews.takeAt(index);
    QProcess* process = m_terminalProcesses.takeAt(index);
    m_terminalTabs->removeTab(index);
    m_pendingTerminalCommands.remove(process);
    if (process->state() != QProcess::NotRunning) {
        process->terminate();
        if (!process->waitForFinished(1000)) {
            process->kill();
            process->waitForFinished(1000);
        }
    }
    process->deleteLater();
    output->deleteLater();

    if (m_terminalViews.isEmpty()) {
        createTerminalSession();
    } else {
        for (int tab = 0; tab < m_terminalTabs->count(); ++tab) {
            m_terminalTabs->setTabText(tab, QStringLiteral("Local %1").arg(tab + 1));
        }
        setActiveTerminalSession(m_terminalTabs->currentIndex());
    }
}

void MainWindow::setActiveTerminalSession(int index) {
    if (index < 0 || index >= m_terminalViews.size() || index >= m_terminalProcesses.size()) {
        m_terminalOutput = nullptr;
        m_terminalProcess = nullptr;
        return;
    }
    m_terminalOutput = m_terminalViews.at(index);
    m_terminalProcess = m_terminalProcesses.at(index);
}

void MainWindow::startTerminal(QProcess* process, QPlainTextEdit* output) {
    if (!process) {
        process = m_terminalProcess;
    }
    if (!output) {
        output = m_terminalOutput;
    }
    if (!process || !output || process->state() != QProcess::NotRunning) {
        return;
    }

    const QString workingDirectory = m_projectRoot.isEmpty() ? QDir::currentPath() : m_projectRoot;
    process->setWorkingDirectory(workingDirectory);
    static_cast<TerminalOutputView*>(output)->setWorkingDirectory(workingDirectory);
#ifdef Q_OS_WIN
    process->start("powershell.exe",
                   {"-NoLogo", "-NoProfile", "-NoExit", "-Command",
                    "[Console]::InputEncoding = New-Object System.Text.UTF8Encoding; "
                    "[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding; "
                    "$OutputEncoding = [Console]::OutputEncoding"});
#else
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("PS1", "sh $PWD> ");
    process->setProcessEnvironment(environment);
    process->start("/bin/sh", {"-i"});
#endif
}

void MainWindow::submitTerminalCommand(const QString& command, QPlainTextEdit* output) {
    if (!output) {
        output = m_terminalOutput;
    }
    if (command.isEmpty() || !output) {
        return;
    }

    const qsizetype index = m_terminalViews.indexOf(output);
    if (index < 0 || index >= m_terminalProcesses.size()) {
        return;
    }
    QProcess* process = m_terminalProcesses.at(index);
    const QString normalizedCommand = command.trimmed().toLower();
    if (normalizedCommand == QStringLiteral("cls") ||
        normalizedCommand == QStringLiteral("clear") ||
        normalizedCommand == QStringLiteral("clear-host")) {
        static_cast<TerminalOutputView*>(output)->clearTerminal();
        static_cast<TerminalOutputView*>(output)->appendOutput(QStringLiteral("PS> "));
        return;
    }

    if (process->state() == QProcess::NotRunning) {
        m_pendingTerminalCommands[process].append(command);
        startTerminal(process, output);
        return;
    }
    if (process->write(command.toUtf8() + "\n") < 0) {
        appendTerminalOutput(
            QStringLiteral("Could not send command: %1\r\n").arg(process->errorString()), output);
    }
}

void MainWindow::appendTerminalOutput(const QString& text, QPlainTextEdit* output) {
    if (!output) {
        output = m_terminalOutput;
    }
    if (!output || text.isEmpty()) {
        return;
    }
    static_cast<TerminalOutputView*>(output)->appendOutput(text);
}

void MainWindow::showGitStatus() {
    if (auto* dock = findChild<QDockWidget*>("GitToolWindow")) {
        dock->show();
        dock->raise();
        m_gitPanel->refresh();
    }
}

void MainWindow::runExternalCommand(const QString& program, const QStringList& arguments,
                                    const QString& description,
                                    const QProcessEnvironment* environment) {
    if (m_commandProcess && m_commandProcess->state() != QProcess::NotRunning) {
        statusBar()->showMessage(QStringLiteral("Wait for the current run command to finish."),
                                 4000);
        return;
    }

    if (!m_commandProcess) {
        m_commandProcess = new QProcess(this);
        m_commandProcess->setProcessChannelMode(QProcess::SeparateChannels);
        connect(m_commandProcess, &QProcess::readyReadStandardOutput, this, [this]() {
            appendTerminalOutput(QString::fromUtf8(m_commandProcess->readAllStandardOutput()));
        });
        connect(m_commandProcess, &QProcess::readyReadStandardError, this, [this]() {
            appendTerminalOutput(QString::fromUtf8(m_commandProcess->readAllStandardError()));
        });
        connect(m_commandProcess, &QProcess::errorOccurred, this,
                [this](QProcess::ProcessError error) {
                    if (error == QProcess::FailedToStart) {
                        appendTerminalOutput(QStringLiteral("Could not start process: %1\r\n")
                                                 .arg(m_commandProcess->errorString()));
                        m_profileAfterRun = false;
                    }
                });
        connect(m_commandProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this](int exitCode, QProcess::ExitStatus status) {
                    appendTerminalOutput(
                        QStringLiteral("\r\nProcess finished (exit code %1, status %2).\r\n")
                            .arg(exitCode)
                            .arg(static_cast<int>(status)));
                    updateDocumentInfo();
                    if (m_profileAfterRun) {
                        const QFileInfoList profiles =
                            QDir(m_profileDirectory)
                                .entryInfoList({QStringLiteral("cachegrind.out.*")}, QDir::Files,
                                               QDir::Time);
                        bool foundProfile = false;
                        for (const QFileInfo& profile : profiles) {
                            if (profile.lastModified().toMSecsSinceEpoch() >= m_profileStartedAt) {
                                appendTerminalOutput(
                                    cachegrindTopFunctions(profile.absoluteFilePath()) +
                                    QStringLiteral("\r\n"));
                                foundProfile = true;
                                break;
                            }
                        }
                        if (!foundProfile) {
                            appendTerminalOutput(
                                QStringLiteral("Xdebug did not produce a profile file. Check "
                                               "Xdebug's profiler configuration.\r\n"));
                        }
                        m_profileAfterRun = false;
                    }
                    if (m_debugServer) {
                        m_debugServer->close();
                    }
                });
    }

    m_commandProcess->setProcessEnvironment(environment ? *environment
                                                        : QProcessEnvironment::systemEnvironment());
    m_commandProcess->setWorkingDirectory(m_projectRoot.isEmpty() ? QDir::currentPath()
                                                                  : m_projectRoot);
    appendTerminalOutput(QStringLiteral("\r\n--- %1 ---\r\n").arg(description));
    m_commandProcess->start(program, arguments);
    QTimer::singleShot(0, this, [this]() { updateDocumentInfo(); });
}
