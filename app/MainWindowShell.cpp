#include <QCheckBox>
#include <QClipboard>
#include <QCryptographicHash>
#include <QFrame>
#include <QSplitter>
#include <QTimer>

#include "DocumentCodec.h"
#include "EditorWidgets.h"
#include "FindBar.h"
#include "forge/core/ThemeCatalog.h"
#include "MainWindow.h"
#include "ProblemsPanel.h"
#include "WorkspaceSupport.h"

void MainWindow::setupMenus() {
    auto menu = [this](const QString& name, const QStringList& ids) {
        auto* result = menuBar()->addMenu(name);
        for (const auto& id : ids)
            if (auto* action = m_actions->action(id)) result->addAction(action);
        return result;
    };
    menu("&File", {"forge.file.new", "forge.file.open", "forge.file.openProject", "forge.file.save",
                   "forge.file.saveAll", "forge.file.close", "forge.app.quit"});
    menu("&Edit", {"forge.edit.undo", "forge.edit.redo", "forge.edit.cut", "forge.edit.copy",
                   "forge.edit.paste", "forge.edit.selectAll", "forge.edit.duplicateLine",
                   "forge.edit.deleteLine", "forge.edit.toggleComment", "forge.edit.moveLineUp",
                   "forge.edit.moveLineDown"});
    auto* view = menu("&View", {"forge.window.projectTree", "forge.window.terminal",
                                "forge.window.problems",
                                "forge.window.restoreLayout"});
    m_themeMenu = view->addMenu("Color Theme");
    refreshThemeMenus();
    menu("&Navigate",
         {"forge.navigate.searchEverywhere", "forge.navigate.goToFile",
          "forge.navigate.recentFiles", "forge.navigate.goToLine", "forge.navigate.find",
          "forge.navigate.findNext", "forge.navigate.findPrevious", "forge.navigate.localReplace",
          "forge.navigate.findInFiles"});
    menu("&Code", {"forge.edit.toggleComment", "forge.code.insertSnippet"});
    menu("&Run", {"forge.run.run", "forge.run.debug", "forge.run.profile", "forge.debug.stepOver",
                  "forge.debug.stepInto", "forge.debug.stepOut", "forge.run.stop"});
    menu("&Tools", {"forge.window.terminal", "forge.terminal.new", "forge.terminal.clear",
                    "forge.window.plugins", "forge.window.settings"});
    menu("&Git", {"forge.vcs.status"});
    menu("&Window", {"forge.window.splitRight", "forge.window.splitDown", "forge.window.closeSplit",
                     "forge.window.restoreLayout", "forge.window.settings"});
    auto* help = menuBar()->addMenu("&Help");
    connect(help->addAction("About Forge IDE"), &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "About Forge IDE",
                           QStringLiteral("Forge IDE %1\nNative PHP / Laravel workspace\nBuilt by "
                                          "Shaker Al-Muzayni\nQt + Rust · Apache-2.0")
                               .arg(FORGE_VERSION));
    });
    menuBar()->hide();
}
void MainWindow::refreshThemeMenus() {
    if (!m_themeMenu) return;
    m_themeMenu->clear();
    const auto themes = forge::core::ThemeCatalog::themes();
    for (auto it = themes.begin(); it != themes.end(); ++it) {
        const auto name = it.key();
        auto* action = m_themeMenu->addAction(name);
        action->setCheckable(true);
        action->setChecked(name == m_themeName);
        connect(action, &QAction::triggered, this, [this, name]() { applyTheme(name); });
    }
}
void MainWindow::setupToolbars() {
    auto* toolbar = addToolBar("Main");
    toolbar->setObjectName("MainToolbar");
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setIconSize(QSize(20, 20));
    auto* hamburger = new QToolButton(toolbar);
    hamburger->setObjectName("MainMenuButton");
    hamburger->setToolTip("Main menu");
    hamburger->setIcon(forgeIcon("menu", QColor("#DFE1E5"), QColor("#3574F0")));
    hamburger->setPopupMode(QToolButton::InstantPopup);
    auto* popup = new QMenu(hamburger);
    for (auto* action : menuBar()->actions())
        if (action->menu()) popup->addMenu(action->menu());
    hamburger->setMenu(popup);
    toolbar->addWidget(hamburger);
    m_projectButton = new QToolButton(toolbar);
    m_projectButton->setObjectName("ProjectSelector");
    m_projectButton->setToolTip("Open or switch project");
    m_projectButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_projectButton->setIcon(forgeIcon("project", QColor("#DFE1E5"), QColor("#B48EED")));
    m_projectButton->setPopupMode(QToolButton::InstantPopup);
    auto* projects = new QMenu(m_projectButton);
    m_projectButton->setMenu(projects);
    m_projectButton->setText("Open Project");
    connect(projects, &QMenu::aboutToShow, this, [this, projects]() {
        projects->clear();
        projects->addAction(m_actions->action("forge.file.openProject"));
        projects->addAction(m_actions->action("forge.file.open"));
        projects->addSeparator();
        const auto recent =
            forge::core::Settings::instance()->value("project.recent").toStringList();
        for (const auto& path : recent) {
            auto* action = projects->addAction(QFileInfo(path).fileName() + "   " + path);
            action->setEnabled(QFileInfo(path).isDir());
            connect(action, &QAction::triggered, this, [this, path]() { setProjectRoot(path); });
        }
    });
    toolbar->addWidget(m_projectButton);
    m_projectLabel = new QLabel(toolbar);
    m_projectLabel->hide();
    m_branchButton = new QToolButton(toolbar);
    m_branchButton->setObjectName("BranchSelector");
    m_branchButton->setText("Git");
    m_branchButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_branchButton->setIcon(forgeIcon("git", QColor("#A8ADB8"), QColor("#A8ADB8")));
    connect(m_branchButton, &QToolButton::clicked, this, [this]() { showGitStatus(); });
    toolbar->addWidget(m_branchButton);
    auto* stretch = new QWidget(toolbar);
    stretch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(stretch);
    m_runConfiguration = new QComboBox(toolbar);
    m_runConfiguration->setObjectName("RunConfigurationSelector");
    m_runConfiguration->addItem("Current File");
    m_runConfiguration->setMinimumWidth(130);
    toolbar->addWidget(m_runConfiguration);
    for (const auto& id : {"forge.run.run", "forge.run.debug", "forge.run.stop"})
        toolbar->addAction(m_actions->action(id));
    toolbar->addSeparator();
    auto* search = toolbar->addAction(forgeIcon("search", QColor("#DFE1E5"), QColor("#3574F0")),
                                      "Search Everywhere");
    search->setObjectName("SearchEverywhereButton");
    search->setToolTip("Search Everywhere (Double Shift / Ctrl+Shift+A)");
    connect(search, &QAction::triggered, this, [this]() { showQuickOpen(); });
    auto* settings = new QToolButton(toolbar);
    settings->setObjectName("SettingsMenuButton");
    settings->setIcon(forgeIcon("settings", QColor("#DFE1E5"), QColor("#3574F0")));
    settings->setToolTip("Settings and Extensions");
    settings->setPopupMode(QToolButton::InstantPopup);
    auto* settingsMenu = new QMenu(settings);
    settingsMenu->addAction(m_actions->action("forge.window.settings"));
    settingsMenu->addAction(m_actions->action("forge.window.plugins"));
    settingsMenu->addMenu(m_themeMenu);
    settingsMenu->addSeparator();
    auto* rtl = settingsMenu->addAction("Right-to-left Layout");
    rtl->setCheckable(true);
    connect(rtl, &QAction::toggled, this, [this](bool enabled) {
        setLayoutDirection(enabled ? Qt::RightToLeft : Qt::LeftToRight);
    });
    settings->setMenu(settingsMenu);
    toolbar->addWidget(settings);
    auto stripe = [this](const QString& name, Qt::ToolBarArea area,
                         const QList<QPair<QString, QString>>& entries) {
        auto* bar = new QToolBar(name, this);
        bar->setObjectName(name);
        bar->setMovable(false);
        bar->setFloatable(false);
        bar->setIconSize(QSize(20, 20));
        addToolBar(area, bar);
        for (const auto& entry : entries) {
            auto* action = m_actions->action(entry.first);
            auto* button = new QToolButton(bar);
            button->setDefaultAction(action);
            button->setProperty("stripe", true);
            button->setFixedSize(36, 36);
            button->setIcon(forgeIcon(entry.second, QColor("#A8ADB8"), QColor("#3574F0")));
            bar->addWidget(button);
            if (entry.first == "forge.window.projectTree" ||
                entry.first == "forge.window.terminal" ||
                entry.first == "forge.window.problems") {
                const auto dockName =
                    entry.first.endsWith("projectTree")
                        ? "ProjectToolWindow"
                        : entry.first.endsWith("problems") ? "ProblemsToolWindow"
                                                           : "TerminalToolWindow";
                if (auto* dock = findChild<QDockWidget*>(dockName)) {
                    action->setCheckable(true);
                    action->setChecked(!dock->isHidden());
                    connect(dock, &QDockWidget::visibilityChanged, action, &QAction::setChecked);
                }
            }
        }
    };
    stripe("LeftStripe", Qt::LeftToolBarArea,
           {{"forge.window.projectTree", "project"},
            {"forge.navigate.findInFiles", "search"},
            {"forge.vcs.status", "git"},
            {"forge.window.plugins", "plugins"},
            {"forge.window.terminal", "terminal"},
            {"forge.window.problems", "problems"}});
    stripe("RightStripe", Qt::RightToolBarArea, {{"forge.window.settings", "settings"}});
}
void MainWindow::setupToolWindows() {
    setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks |
                   QMainWindow::AnimatedDocks);
    setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    m_findBar = new FindBar(central);
    centralLayout->addWidget(m_findBar);
    m_centerStack = new QStackedWidget(central);
    centralLayout->addWidget(m_centerStack, 1);
    setCentralWidget(central);
    m_welcomePage = new QWidget(m_centerStack);
    auto* welcome = new QVBoxLayout(m_welcomePage);
    welcome->setAlignment(Qt::AlignCenter);
    welcome->setSpacing(16);
    auto* title = new QLabel("Forge IDE", m_welcomePage);
    title->setObjectName("WelcomeTitle");
    title->setAlignment(Qt::AlignCenter);
    welcome->addWidget(title);
    auto* caption = new QLabel("Your next idea starts here.", m_welcomePage);
    caption->setAlignment(Qt::AlignCenter);
    welcome->addWidget(caption);
    for (const auto& entry : QList<QPair<QString, QString>>{
             {"Open Project", "forge.file.openProject"},
             {"Open File", "forge.file.open"},
             {"Search Everywhere   Double Shift", "forge.navigate.searchEverywhere"}}) {
        auto* button = new QPushButton(entry.first, m_welcomePage);
        button->setMinimumWidth(260);
        welcome->addWidget(button, 0, Qt::AlignHCenter);
        connect(button, &QPushButton::clicked, m_actions->action(entry.second), &QAction::trigger);
    }
    m_centerStack->addWidget(m_welcomePage);
    m_editorTabs = new QTabWidget(m_centerStack);
    m_editorTabs->setObjectName("EditorTabs");
    m_editorTabs->setDocumentMode(true);
    m_editorTabs->setTabsClosable(true);
    m_editorTabs->setMovable(true);
    connect(m_editorTabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeDocument);
    connect(m_editorTabs, &QTabWidget::currentChanged, this, [this](int) {
        syncSplitEditor();
        m_findBar->setEditor(currentEditor());
        updateEditorStatus();
        updateProblemsCurrentDocument();
    });
    m_editorTabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_editorTabs->tabBar(), &QWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
                const int selected = m_editorTabs->tabBar()->tabAt(pos);
                if (selected < 0) return;
                m_editorTabs->setCurrentIndex(selected);
                auto* keep = m_editorTabs->widget(selected);
                QMenu menu(this);
                auto* close = menu.addAction("Close");
                auto* others = menu.addAction("Close Other Tabs");
                auto* all = menu.addAction("Close All Tabs");
                auto* reveal = menu.addAction("Reveal in Project");
                auto* copy = menu.addAction("Copy Path");
                auto* splitRight = menu.addAction("Split Right");
                auto* splitDown = menu.addAction("Split Down");
                auto* unsplit = menu.addAction("Close Split");
                unsplit->setEnabled(m_splitEditor != nullptr);
                const auto* chosen = menu.exec(m_editorTabs->tabBar()->mapToGlobal(pos));
                if (chosen == close) closeDocument(selected);
                if (chosen == all || chosen == others)
                    for (int i = m_editorTabs->count() - 1; i >= 0; --i) {
                        if (chosen == others && m_editorTabs->widget(i) == keep) continue;
                        const int before = m_editorTabs->count();
                        closeDocument(i);
                        if (before == m_editorTabs->count()) break;
                    }
                const auto path = keep->property("filePath").toString();
                if (chosen == reveal) {
                    m_projectTree->setCurrentIndex(m_fileSystemModel->index(path));
                    m_projectTree->scrollTo(m_projectTree->currentIndex());
                }
                if (chosen == copy) QApplication::clipboard()->setText(path);
                if (chosen == splitRight) splitEditor(Qt::Horizontal);
                if (chosen == splitDown) splitEditor(Qt::Vertical);
                if (chosen == unsplit && m_splitEditor) {
                    auto* split = m_splitEditor;
                    m_splitEditor = nullptr;
                    split->deleteLater();
                    m_editorTabs->setFocus();
                }
            });
    m_editorSplitter = new QSplitter(Qt::Horizontal, m_centerStack);
    m_editorSplitter->setObjectName("EditorSplitter");
    m_editorSplitter->addWidget(m_editorTabs);
    m_editorSplitter->setChildrenCollapsible(false);
    m_centerStack->addWidget(m_editorSplitter);
    m_fileSystemModel = new ForgeFileSystemModel(this);
    m_fileIconProvider =
        std::make_unique<ForgeFileIconProvider>(QColor("#BCBEC4"), QColor("#3574F0"));
    m_fileSystemModel->setIconProvider(m_fileIconProvider.get());
    m_fileSystemModel->setFilter(QDir::AllDirs | QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    m_projectTree = new QTreeView(this);
    m_projectTree->setObjectName("ProjectTree");
    m_projectTree->setModel(m_fileSystemModel);
    m_projectTree->setHeaderHidden(true);
    m_projectTree->setIndentation(18);
    m_projectTree->setUniformRowHeights(true);
    m_projectTree->setAnimated(true);
    for (int i = 1; i < 4; ++i) m_projectTree->hideColumn(i);
    m_projectTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_projectTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_projectTree, &QTreeView::doubleClicked, this, &MainWindow::openFile);
    connect(m_projectTree, &QTreeView::customContextMenuRequested, this,
            &MainWindow::projectContextMenu);
    auto* projectDock = new QDockWidget("Project", this);
    projectDock->setObjectName("ProjectToolWindow");
    projectDock->setWidget(m_projectTree);
    addDockWidget(Qt::LeftDockWidgetArea, projectDock);
    auto* header = new QWidget(projectDock);
    auto* headerRow = new QHBoxLayout(header);
    headerRow->setContentsMargins(10, 3, 4, 3);
    headerRow->addWidget(new QLabel("Project", header));
    headerRow->addStretch();
    for (const auto& entry : QList<QPair<QString, QString>>{
             {"◎", "Select Open File"}, {"⌃", "Collapse All"}, {"−", "Hide Project"}}) {
        auto* button = new QToolButton(header);
        button->setText(entry.first);
        button->setToolTip(entry.second);
        headerRow->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, projectDock, title = entry.second]() {
            if (title == "Hide Project")
                projectDock->hide();
            else if (title == "Collapse All")
                m_projectTree->collapseAll();
            else if (currentEditor()) {
                const auto index =
                    m_fileSystemModel->index(currentEditor()->property("filePath").toString());
                m_projectTree->setCurrentIndex(index);
                m_projectTree->scrollTo(index);
            }
        });
    }
    projectDock->setTitleBarWidget(header);
    auto* terminalDock = new QDockWidget("Terminal", this);
    terminalDock->setObjectName("TerminalToolWindow");
    terminalDock->setTitleBarWidget(new QWidget(terminalDock));
    auto* panel = new QWidget(terminalDock);
    auto* terminalLayout = new QVBoxLayout(panel);
    terminalLayout->setContentsMargins(0, 0, 0, 0);
    terminalLayout->setSpacing(0);
    auto* terminalHeader = new QHBoxLayout;
    terminalHeader->setContentsMargins(10, 4, 4, 2);
    terminalHeader->addWidget(new QLabel("Terminal", panel));
    terminalHeader->addStretch();
    auto* plus = new QToolButton(panel);
    plus->setText("+");
    plus->setToolTip("New Terminal");
    terminalHeader->addWidget(plus);
    connect(plus, &QToolButton::clicked, this, &MainWindow::createTerminalSession);
    auto* hide = new QToolButton(panel);
    hide->setText("−");
    hide->setToolTip("Hide Terminal");
    terminalHeader->addWidget(hide);
    connect(hide, &QToolButton::clicked, terminalDock, &QDockWidget::hide);
    terminalLayout->addLayout(terminalHeader);
    m_terminalTabs = new QTabWidget(panel);
    m_terminalTabs->setObjectName("TerminalSessionTabs");
    m_terminalTabs->setDocumentMode(true);
    terminalLayout->addWidget(m_terminalTabs);
    connect(m_terminalTabs, &QTabWidget::currentChanged, this,
            &MainWindow::setActiveTerminalSession);
    terminalDock->setWidget(panel);
    addDockWidget(Qt::BottomDockWidgetArea, terminalDock);
    auto* problemsDock = new QDockWidget("Problems", this);
    problemsDock->setObjectName("ProblemsToolWindow");
    m_problemsPanel = new ProblemsPanel(
        &m_diagnostics,
        forge::core::Settings::instance()->value("diagnostics.phpLintOnSave", true).toBool(),
        problemsDock);
    m_phpLintEnabled =
        forge::core::Settings::instance()->value("diagnostics.phpLintOnSave", true).toBool();
    problemsDock->setWidget(m_problemsPanel);
    addDockWidget(Qt::BottomDockWidgetArea, problemsDock);
    tabifyDockWidget(terminalDock, problemsDock);
    problemsDock->hide();
    connect(m_problemsPanel, &ProblemsPanel::locationActivated, this,
            [this](const QString& file, int line, int column) {
                openFilePath(file);
                if (auto* editor = currentEditor()) {
                    const QTextBlock block = editor->document()->findBlockByNumber(line - 1);
                    if (block.isValid()) {
                        QTextCursor cursor(block);
                        cursor.setPosition(block.position() +
                                           qBound(0, column - 1, block.text().size()));
                        editor->setTextCursor(cursor);
                        editor->centerCursor();
                        editor->setFocus();
                    }
                }
            });
    connect(m_problemsPanel, &ProblemsPanel::phpLintToggled, this, [this](bool enabled) {
        m_phpLintEnabled = enabled;
        forge::core::Settings::instance()->setValue("diagnostics.phpLintOnSave", enabled);
        forge::core::Settings::instance()->save();
        if (enabled) {
            m_diagnostics.setStatusMessage(QString());
        } else {
            m_diagnostics.clearSource(QStringLiteral("php -l"));
        }
    });
    createTerminalSession();
    resizeDocks({projectDock}, {275}, Qt::Horizontal);
    resizeDocks({terminalDock}, {220}, Qt::Vertical);
}
void MainWindow::setupStatusBar() {
    statusBar()->setSizeGripEnabled(false);
    m_breadcrumbs = new QLabel("Forge IDE", this);
    m_breadcrumbs->setMinimumWidth(0);
    m_breadcrumbs->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusBar()->addWidget(m_breadcrumbs, 1);
    m_statusLabel = new QLabel("Analyzer: starting…", this);
    statusBar()->addPermanentWidget(m_statusLabel);
    m_fileTypeLabel = new QLabel("Text", this);
    m_caretLabel = new QLabel("1:1", this);
    statusBar()->addPermanentWidget(m_fileTypeLabel);
    statusBar()->addPermanentWidget(m_caretLabel);
    auto button = [this](const QString& name, const QString& text) {
        auto* widget = new QToolButton(this);
        widget->setObjectName(name);
        widget->setText(text);
        widget->setAutoRaise(true);
        statusBar()->addPermanentWidget(widget);
        return widget;
    };
    m_eolButton = button("LineEndingStatus", "LF");
    m_encodingButton = button("EncodingStatus", "UTF-8");
    m_indentButton = button("IndentStatus", "4 spaces");
    m_readOnlyButton = button("ReadOnlyStatus", "RW");
    m_problemCountButton = button("ProblemCountStatus", "✖ 0  ⚠ 0");
    m_problemCountButton->setToolTip("Open Problems");
    connect(m_problemCountButton, &QToolButton::clicked, this, [this]() {
        m_actions->action(QStringLiteral("forge.window.problems"))->trigger();
    });
    m_readOnlyButton->setToolTip("Toggle editor read-only mode");
    connect(m_readOnlyButton, &QToolButton::clicked, this, [this]() {
        if (auto* editor = currentEditor()) {
            editor->setReadOnly(!editor->isReadOnly());
            updateDocumentInfo();
        }
    });
    connect(m_eolButton, &QToolButton::clicked, this, [this]() {
        auto* editor = currentEditor();
        if (!editor || editor->isReadOnly()) return;
        QMenu menu(this);
        for (const auto& entry :
             QList<QPair<QString, forge::core::LineEnding>>{{"LF", forge::core::LineEnding::Lf},
                                                            {"CRLF", forge::core::LineEnding::CrLf},
                                                            {"CR", forge::core::LineEnding::Cr}}) {
            auto* action = menu.addAction(entry.first);
            connect(action, &QAction::triggered, this, [this, editor, ending = entry.second]() {
                QVector<forge::core::LineEnding> endings(
                    qMax(0, editor->document()->blockCount() - 1), ending);
                storeLineEndings(editor->document(), endings);
                editor->document()->setModified(true);
                updateDocumentInfo();
            });
        }
        menu.exec(m_eolButton->mapToGlobal(QPoint(0, 0)));
    });
    connect(m_encodingButton, &QToolButton::clicked, this, [this]() {
        auto* editor = currentEditor();
        if (!editor || editor->isReadOnly()) return;
        QMenu menu(this);
        for (int i = 0; i < 6; ++i) {
            const auto encoding = static_cast<forge::core::TextEncoding>(i);
            auto* action =
                menu.addAction("Convert to " + forge::core::TextFileCodec::encodingName(encoding));
            connect(action, &QAction::triggered, this, [this, editor, i]() {
                editor->setProperty("textEncoding", i);
                editor->document()->setModified(true);
                updateDocumentInfo();
            });
        }
        menu.exec(m_encodingButton->mapToGlobal(QPoint(0, 0)));
    });
    connect(m_indentButton, &QToolButton::clicked, this, [this]() {
        auto* editor = currentEditor();
        if (!editor) return;
        QMenu menu(this);
        for (int size : {2, 4, 8}) {
            auto* action = menu.addAction(QStringLiteral("%1 spaces").arg(size));
            connect(action, &QAction::triggered, this, [this, editor, size]() {
                editor->setProperty("indentSize", size);
                editor->setTabStopDistance(QFontMetrics(editor->font()).horizontalAdvance(' ') *
                                           size);
                updateDocumentInfo();
            });
        }
        menu.exec(m_indentButton->mapToGlobal(QPoint(0, 0)));
    });
}
void MainWindow::restoreDefaultLayout() {
    auto* project = findChild<QDockWidget*>("ProjectToolWindow");
    auto* terminal = findChild<QDockWidget*>("TerminalToolWindow");
    if (project && terminal) {
        addDockWidget(Qt::LeftDockWidgetArea, project);
        addDockWidget(Qt::BottomDockWidgetArea, terminal);
        project->setFloating(false);
        terminal->setFloating(false);
        project->show();
        terminal->show();
        resizeDocks({project}, {275}, Qt::Horizontal);
        resizeDocks({terminal}, {220}, Qt::Vertical);
    }
}
