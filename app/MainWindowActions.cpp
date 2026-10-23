#include "DocumentCodec.h"
#include "EditorWidgets.h"
#include "FindBar.h"
#include "MainWindow.h"
#include "TerminalView.h"
#include "WorkspaceSupport.h"

constexpr int kWindowLayoutVersion = 3;

void MainWindow::setupActions() {
    m_actions->registerAction(QStringLiteral("forge.app.quit"), QStringLiteral("Exit"),
                              [this]() { close(); });
    m_actions->registerAction(QStringLiteral("forge.file.openProject"),
                              QStringLiteral("Open Project..."), [this]() { openProject(); });
    m_actions->registerAction(
        QStringLiteral("forge.file.open"), QStringLiteral("Open File..."), [this]() {
            const QString path =
                QFileDialog::getOpenFileName(this, QStringLiteral("Open File"), m_projectRoot);
            if (!path.isEmpty()) {
                openFilePath(path);
            }
        });
    m_actions->registerAction(QStringLiteral("forge.file.new"), QStringLiteral("New File"),
                              [this]() { createUntitledDocument(); });
    m_actions->registerAction(QStringLiteral("forge.file.save"), QStringLiteral("Save"),
                              [this]() { saveCurrentDocument(); });
    m_actions->registerAction(QStringLiteral("forge.file.close"), QStringLiteral("Close Tab"),
                              [this]() {
                                  const int index = m_editorTabs->currentIndex();
                                  if (index >= 0) {
                                      closeDocument(index);
                                  }
                              });
    auto registerEditorAction = [this](const QString& id, const QString& title,
                                       const std::function<void(QPlainTextEdit*)>& operation) {
        m_actions->registerAction(id, title, [this, operation]() {
            auto* focusedEditor = qobject_cast<QPlainTextEdit*>(QApplication::focusWidget());
            QPlainTextEdit* editor = focusedEditor ? focusedEditor : currentEditor();
            if (editor) {
                operation(editor);
            }
        });
    };
    registerEditorAction(QStringLiteral("forge.edit.undo"), QStringLiteral("Undo"),
                         [](QPlainTextEdit* editor) { editor->undo(); });
    registerEditorAction(QStringLiteral("forge.edit.redo"), QStringLiteral("Redo"),
                         [](QPlainTextEdit* editor) { editor->redo(); });
    registerEditorAction(QStringLiteral("forge.edit.cut"), QStringLiteral("Cut"),
                         [](QPlainTextEdit* editor) { editor->cut(); });
    registerEditorAction(QStringLiteral("forge.edit.copy"), QStringLiteral("Copy"),
                         [](QPlainTextEdit* editor) { editor->copy(); });
    registerEditorAction(QStringLiteral("forge.edit.paste"), QStringLiteral("Paste"),
                         [](QPlainTextEdit* editor) { editor->paste(); });
    registerEditorAction(QStringLiteral("forge.edit.selectAll"), QStringLiteral("Select All"),
                         [](QPlainTextEdit* editor) { editor->selectAll(); });
    m_actions->registerAction(QStringLiteral("forge.navigate.find"), QStringLiteral("Find..."),
                              [this]() { findInCurrentDocument(); });
    m_actions->registerAction(QStringLiteral("forge.navigate.findNext"),
                              QStringLiteral("Find Next"),
                              [this]() { findNextInCurrentDocument(); });
    m_actions->registerAction(QStringLiteral("forge.navigate.goToLine"),
                              QStringLiteral("Go to Line..."), [this]() { goToLine(); });
    m_actions->registerAction(QStringLiteral("forge.navigate.goToFile"),
                              QStringLiteral("Go to File..."), [this]() { goToFile(); });
    m_actions->registerAction(QStringLiteral("forge.navigate.recentFiles"),
                              QStringLiteral("Recent Files"), [this]() { showRecentFiles(); });
    m_actions->registerAction(
        QStringLiteral("forge.terminal.clear"), QStringLiteral("Clear Terminal"), [this]() {
            if (m_terminalOutput) {
                static_cast<TerminalOutputView*>(m_terminalOutput)->clearTerminal();
            }
        });
    m_actions->registerAction(
        QStringLiteral("forge.terminal.new"), QStringLiteral("New Terminal"), [this]() {
            if (auto* dock = findChild<QDockWidget*>(QStringLiteral("TerminalToolWindow"))) {
                dock->setVisible(!dock->isVisible());
                if (dock->isVisible()) dock->raise();
            }
            createTerminalSession();
        });
    m_actions->registerAction(
        QStringLiteral("forge.terminal.copy"), QStringLiteral("Copy Terminal Selection"), [this]() {
            if (m_terminalOutput && m_terminalOutput->textCursor().hasSelection()) {
                m_terminalOutput->copy();
            }
        });
    m_actions->registerAction(QStringLiteral("forge.terminal.paste"),
                              QStringLiteral("Paste into Terminal"), [this]() {
                                  if (m_terminalOutput) {
                                      m_terminalOutput->paste();
                                  }
                              });
    m_actions->registerAction(QStringLiteral("forge.run.run"),
                              QStringLiteral("Run Current PHP File"),
                              [this]() { runCurrentPhpFile(); });
    m_actions->registerAction(QStringLiteral("forge.run.debug"),
                              QStringLiteral("Debug Current PHP File"),
                              [this]() { debugCurrentPhpFile(); });
    m_actions->registerAction(QStringLiteral("forge.run.profile"),
                              QStringLiteral("Profile Current PHP File"),
                              [this]() { profileCurrentPhpFile(); });
    m_actions->registerAction(QStringLiteral("forge.debug.stepOver"), QStringLiteral("Step Over"),
                              [this]() { sendDebuggerStep(QStringLiteral("step_over")); });
    m_actions->registerAction(QStringLiteral("forge.debug.stepInto"), QStringLiteral("Step Into"),
                              [this]() { sendDebuggerStep(QStringLiteral("step_into")); });
    m_actions->registerAction(QStringLiteral("forge.debug.stepOut"), QStringLiteral("Step Out"),
                              [this]() { sendDebuggerStep(QStringLiteral("step_out")); });
    m_actions->registerAction(QStringLiteral("forge.run.stop"), QStringLiteral("Stop"), [this]() {
        if (m_debugSocket && m_debugSocket->state() == QAbstractSocket::ConnectedState) {
            const QByteArray stopCommand =
                QStringLiteral("stop -i %1").arg(m_debugTransactionId++).toUtf8() + '\0';
            m_debugSocket->write(stopCommand);
        }
        if (m_commandProcess && m_commandProcess->state() != QProcess::NotRunning) {
            m_commandProcess->terminate();
        }
        if (m_debugServer) {
            m_debugServer->close();
        }
    });
    m_actions->registerAction(QStringLiteral("forge.vcs.status"), QStringLiteral("Git Status"),
                              [this]() { showGitStatus(); });
    m_actions->registerAction(
        QStringLiteral("forge.window.projectTree"), QStringLiteral("Project"), [this]() {
            if (auto* dock = findChild<QDockWidget*>(QStringLiteral("ProjectToolWindow"))) {
                dock->setVisible(!dock->isVisible());
                if (dock->isVisible()) dock->raise();
            }
        });
    m_actions->registerAction(
        QStringLiteral("forge.window.terminal"), QStringLiteral("Terminal"), [this]() {
            if (auto* dock = findChild<QDockWidget*>(QStringLiteral("TerminalToolWindow"))) {
                dock->setVisible(!dock->isVisible());
                if (dock->isVisible()) dock->raise();
                startTerminal();
                m_terminalOutput->setFocus();
            }
        });
    m_actions->registerAction(QStringLiteral("forge.window.settings"),
                              QStringLiteral("Settings..."), [this]() { showSettings(); });
    m_actions->registerAction(QStringLiteral("forge.window.plugins"),
                              QStringLiteral("Plugin Manager..."),
                              [this]() { showPluginManager(); });
    m_actions->registerAction(QStringLiteral("forge.window.restoreLayout"),
                              QStringLiteral("Restore Default Layout"),
                              [this]() { restoreDefaultLayout(); });
    m_actions->registerAction("forge.window.splitRight", "Split Right",
                              [this]() { splitEditor(Qt::Horizontal); });
    m_actions->registerAction("forge.window.splitDown", "Split Down",
                              [this]() { splitEditor(Qt::Vertical); });
    m_actions->registerAction("forge.window.closeSplit", "Close Split", [this]() {
        if (m_splitEditor) {
            auto* view = m_splitEditor;
            m_splitEditor = nullptr;
            view->deleteLater();
            m_editorTabs->setFocus();
        }
    });
    m_actions->registerAction("forge.file.saveAll", "Save All", [this]() {
        for (int i = 0; i < m_editorTabs->count(); ++i) {
            auto* editor = qobject_cast<QPlainTextEdit*>(m_editorTabs->widget(i));
            if (editor && editor->document()->isModified() && !saveDocument(editor)) break;
        }
    });
    m_actions->registerAction("forge.navigate.localReplace", "Replace in File…",
                              [this]() { m_findBar->open(currentEditor(), true); });
    m_actions->registerAction("forge.navigate.findPrevious", "Find Previous",
                              [this]() { m_findBar->find(true); });
    m_actions->registerAction("forge.navigate.searchEverywhere", "Search Everywhere",
                              [this]() { showQuickOpen(); });
    m_actions->registerAction("forge.navigate.findInFiles", "Find in Files…",
                              [this]() { showFindInFiles(); });
    m_actions->registerAction("forge.code.insertSnippet", "Insert Snippet…",
                              [this]() { insertSnippet(); });
    for (const auto& entry :
         QList<QPair<QString, QString>>{{"duplicateLine", "Duplicate Line"},
                                        {"deleteLine", "Delete Line"},
                                        {"toggleComment", "Toggle Line Comment"},
                                        {"moveLineUp", "Move Line Up"},
                                        {"moveLineDown", "Move Line Down"}}) {
        const auto operation = entry.first;
        m_actions->registerAction("forge.edit." + operation, entry.second,
                                  [this, operation]() { editLine(operation); });
    }
    m_actions->registerAction(QStringLiteral("forge.theme.graphite"), QStringLiteral("Graphite"),
                              [this]() { applyTheme(QStringLiteral("Graphite")); });
    m_actions->registerAction(QStringLiteral("forge.theme.daylight"), QStringLiteral("Daylight"),
                              [this]() { applyTheme(QStringLiteral("Daylight")); });
}

void MainWindow::setupShortcuts() {
    for (const QString& id : m_actions->allActionIds()) {
        QAction* action = m_actions->action(id);
        const bool terminalAction = id.startsWith(QStringLiteral("forge.terminal."));
        action->setShortcutContext(terminalAction ? Qt::WidgetWithChildrenShortcut
                                                  : Qt::ApplicationShortcut);
        if (!terminalAction) addAction(action);
        if (terminalAction) {
            for (QPlainTextEdit* terminalView : std::as_const(m_terminalViews)) {
                terminalView->addAction(action);
            }
        }
    }
}
