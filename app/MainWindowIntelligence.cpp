#include <QTextBlock>

#include "DatabasePanel.h"
#include "GitPanel.h"
#include "InspectionPanel.h"
#include "MainWindow.h"
#include "PhpEditorController.h"
#include "WorkspaceSupport.h"

void MainWindow::setupIntelligence() {
    auto* gitDock = new QDockWidget("Git", this);
    gitDock->setObjectName("GitToolWindow");
    m_gitPanel = new GitPanel(gitDock);
    gitDock->setWidget(m_gitPanel);
    addDockWidget(Qt::BottomDockWidgetArea, gitDock);
    if (auto* terminal = findChild<QDockWidget*>("TerminalToolWindow"))
        tabifyDockWidget(terminal, gitDock);
    gitDock->hide();
    auto* databaseDock = new QDockWidget("Database", this);
    databaseDock->setObjectName("DatabaseToolWindow");
    databaseDock->setWidget(new DatabasePanel(databaseDock));
    addDockWidget(Qt::BottomDockWidgetArea, databaseDock);
    tabifyDockWidget(gitDock, databaseDock);
    databaseDock->hide();
    m_actions->registerAction("forge.window.database", "Database", [databaseDock] {
        databaseDock->show();
        databaseDock->raise();
    });
    auto* inspectionDock = new QDockWidget("Code Quality", this);
    inspectionDock->setObjectName("InspectionToolWindow");
    m_inspections = new InspectionPanel(&m_diagnostics, inspectionDock);
    inspectionDock->setWidget(m_inspections);
    addDockWidget(Qt::BottomDockWidgetArea, inspectionDock);
    tabifyDockWidget(gitDock, inspectionDock);
    inspectionDock->hide();
    m_actions->registerAction("forge.tools.inspect", "PHPStan / PHP_CodeSniffer…",
                              [inspectionDock] {
                                  inspectionDock->show();
                                  inspectionDock->raise();
                              });
    m_phpController = new PhpEditorController(this, &m_diagnostics);
    m_phpController->message = [this](const QString& text) {
        statusBar()->showMessage(text, 10000);
    };
    m_phpController->openLocation = [this](const QString& file, int line, int column) {
        openFilePath(file);
        if (auto* editor = currentEditor()) {
            const auto block = editor->document()->findBlockByNumber(line);
            if (!block.isValid()) return;
            QTextCursor cursor(block);
            cursor.setPosition(block.position() +
                               qBound(0, column, static_cast<int>(block.text().size())));
            editor->setTextCursor(cursor);
            editor->centerCursor();
            editor->setFocus();
        }
    };
    auto action = [this](const QString& id, const QString& label, const QKeySequence& key,
                         auto callback) {
        m_actions->registerAction(id, label, callback);
        auto* registered = m_actions->action(id);
        if (!key.isEmpty()) registered->setShortcut(key);
        addAction(registered);
    };
    action("forge.php.configure", "PHP Intelligence…", {},
           [this] { m_phpController->configure(); });
    action("forge.php.reindex", "Reindex PHP Project", {},
           [this] { m_phpController->setWorkspace(m_projectRoot, true); });
    action("forge.code.complete", "Complete Code", QKeySequence(),
           [this] { m_phpController->complete(currentEditor()); });
    action("forge.navigate.definition", "Go to Definition", QKeySequence("Ctrl+B"),
           [this] { m_phpController->definition(currentEditor()); });
    action("forge.navigate.symbols", "Find Class or Symbol…", QKeySequence("Ctrl+Alt+N"),
           [this] { m_phpController->symbols(); });
    action("forge.code.format", "Format PHP Document", QKeySequence("Ctrl+Alt+L"),
           [this] { m_phpController->format(currentEditor()); });
    auto* definition = m_actions->action("forge.navigate.definition");
    definition->setShortcuts({QKeySequence("Ctrl+B"), QKeySequence("F12")});
}
