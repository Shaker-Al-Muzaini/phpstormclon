#include <QCheckBox>
#include <QPalette>
#include <QSpinBox>
#include <QTreeWidget>

#include "DocumentCodec.h"
#include "EditorWidgets.h"
#include "ExtensionsDialog.h"
#include "forge/core/ThemeCatalog.h"
#include "MainWindow.h"
#include "TerminalView.h"
#include "WorkspaceSupport.h"

constexpr int kWindowLayoutVersion = 3;

void MainWindow::applyTheme(const QString& name) {
    QFile file(themeResourcePath(name));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[MainWindow] Cannot open theme resource:" << file.fileName();
        return;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning() << "[MainWindow] Invalid theme JSON:" << error.errorString();
        return;
    }

    const QJsonObject root = document.object();
    m_themeName = name;
    const QJsonObject ui = root.value(QStringLiteral("ui")).toObject();
    const QJsonObject metrics = root.value(QStringLiteral("metrics")).toObject();
    const QString background =
        themedColor(ui, QStringLiteral("background"), QStringLiteral("#1E1F22"));
    const QString elevated = themedColor(ui, QStringLiteral("backgroundElevated"), background);
    const QString panel = themedColor(ui, QStringLiteral("backgroundPanel"), elevated);
    const QString foreground =
        themedColor(ui, QStringLiteral("foreground"), QStringLiteral("#DFE1E5"));
    const QString muted = themedColor(ui, QStringLiteral("foregroundMuted"), foreground);
    const QString border = themedColor(ui, QStringLiteral("border"), QStringLiteral("#393B40"));
    const QString accent = themedColor(ui, QStringLiteral("accent"), QStringLiteral("#4D78CC"));
    const QString selection = themedColor(ui, QStringLiteral("backgroundSelection"), accent);
    const QString fontFamily =
        metrics.value(QStringLiteral("uiFontFamily")).toString(QStringLiteral("Segoe UI"));
    const int fontSize = metrics.value(QStringLiteral("uiFontSize")).toInt(10);
    const int iconSize = metrics.value(QStringLiteral("iconSize")).toInt(16);
    const QString style =
        QStringLiteral(
            "QWidget { background: %1; color: %2; font-family: \"%3\"; font-size: %4pt; }"
            "QMenuBar, QToolBar, QDockWidget::title { background: %5; }"
            "QMenuBar::item { padding: 4px 6px; }"
            "QToolBar { spacing: 3px; padding: 2px; border: 0; }"
            "QToolButton { padding: 4px; border-radius: 4px; }"
            "QToolButton:hover { background: %6; }"
            "QMenuBar::item:selected, QMenu::item:selected, QTreeView::item:hover {"
            " background: %6; }"
            "QMenu { border: 1px solid %7; }"
            "QMenu::item { padding: 5px 28px 5px 18px; }"
            "QPlainTextEdit, QTreeView, QTabWidget::pane { background: %8; }"
            "QScrollBar:vertical { background: %8; width: 12px; margin: 0; }"
            "QScrollBar:horizontal { background: %8; height: 12px; margin: 0; }"
            "QScrollBar::handle:vertical, QScrollBar::handle:horizontal {"
            " background: %7; min-width: 24px; min-height: 24px; border-radius: 5px; margin: 2px; }"
            "QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover {"
            " background: %6; }"
            "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }"
            "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }"
            "QTreeView::item:selected, QTabBar::tab:selected { background: %9; }"
            "QTabBar::tab { background: %5; padding: 7px 12px; border: 1px solid %7; }"
            "QTabWidget#TerminalSessionTabs::pane { border: 0; }"
            "QTabWidget#TerminalSessionTabs QTabBar::tab {"
            " background: transparent; border: 0; border-bottom: 2px solid transparent;"
            " padding: 5px 8px; }"
            "QTabWidget#TerminalSessionTabs QTabBar::tab:selected {"
            " background: transparent; border-bottom-color: %10; }"
            "QDockWidget::title { padding: 5px; }"
            "QPushButton, QComboBox { background: %5; border: 1px solid %7; padding: 6px 10px; }"
            "QPushButton:hover { border-color: %10; }"
            "QStatusBar { background: %11; color: %12; }")
            .arg(background, foreground, fontFamily)
            .arg(fontSize)
            .arg(elevated, selection, border, panel, selection, accent,
                 themedColor(ui, QStringLiteral("statusBar"), elevated),
                 themedColor(ui, QStringLiteral("statusBarForeground"), muted));
    qApp->setStyleSheet(style);
    for (QPlainTextEdit* editor : findChildren<QPlainTextEdit*>()) {
        if (editor->property("forgeSourceEditor").toBool()) {
            applyEditorTheme(editor);
        }
    }
    for (QToolBar* toolbar : findChildren<QToolBar*>()) {
        toolbar->setIconSize(QSize(iconSize, iconSize));
    }
    if (m_terminalTabs) {
        const QString closeButtonHover =
            m_themeName.compare(QStringLiteral("Daylight"), Qt::CaseInsensitive) == 0
                ? QStringLiteral("#E1E5EC")
                : QStringLiteral("#393B40");
        for (int index = 0; index < m_terminalTabs->count(); ++index) {
            auto* closeButton = qobject_cast<QToolButton*>(
                m_terminalTabs->tabBar()->tabButton(index, QTabBar::RightSide));
            if (closeButton) {
                closeButton->setIcon(
                    forgeIcon(QStringLiteral("close"), QColor(muted), QColor(foreground)));
                closeButton->setStyleSheet(
                    QStringLiteral("QToolButton { border: 0; border-radius: 3px; padding: 2px; }"
                                   "QToolButton:hover { background: %1; }")
                        .arg(closeButtonHover));
            }
        }
    }

    const QColor iconForeground(foreground);
    const QColor iconAccent(accent);
    const std::array<std::pair<QString, QString>, 10> actionIcons{
        {{QStringLiteral("forge.file.openProject"), QStringLiteral("project")},
         {QStringLiteral("forge.file.open"), QStringLiteral("fileOpen")},
         {QStringLiteral("forge.file.save"), QStringLiteral("save")},
         {QStringLiteral("forge.file.new"), QStringLiteral("file")},
         {QStringLiteral("forge.window.settings"), QStringLiteral("settings")},
         {QStringLiteral("forge.window.terminal"), QStringLiteral("terminal")},
         {QStringLiteral("forge.run.run"), QStringLiteral("run")},
         {QStringLiteral("forge.run.debug"), QStringLiteral("debug")},
         {QStringLiteral("forge.run.stop"), QStringLiteral("stop")},
         {QStringLiteral("forge.vcs.status"), QStringLiteral("git")}}};
    for (const auto& [actionId, iconName] : actionIcons) {
        if (QAction* action = m_actions->action(actionId)) {
            action->setIcon(forgeIcon(iconName, iconForeground, iconAccent));
            action->setIconVisibleInMenu(true);
            action->setToolTip(action->text());
            action->setStatusTip(action->text());
        }
    }
    if (auto* toolbar = findChild<QToolBar*>(QStringLiteral("MainToolbar"))) {
        for (QAction* action : toolbar->actions()) {
            if (action->text() == QStringLiteral("Go to File")) {
                action->setIcon(forgeIcon(QStringLiteral("search"), iconForeground, iconAccent));
            } else if (action->text() == QStringLiteral("Debug")) {
                action->setIcon(forgeIcon(QStringLiteral("run"), iconForeground, iconAccent));
            }
        }
    }

    const QIcon appIcon = forgeIcon(QStringLiteral("forge"), iconForeground, iconAccent);
    setWindowIcon(appIcon);
    qApp->setWindowIcon(appIcon);
    if (auto* provider = dynamic_cast<ForgeFileIconProvider*>(m_fileIconProvider.get())) {
        provider->setColors(QColor(muted), iconAccent);
        m_projectTree->viewport()->update();
    }
    if (auto* button = findChild<QPushButton*>(QStringLiteral("openProjectButton"))) {
        button->setIcon(forgeIcon(QStringLiteral("project"), iconForeground, iconAccent));
    }
    if (auto* button = findChild<QPushButton*>(QStringLiteral("openFileButton"))) {
        button->setIcon(forgeIcon(QStringLiteral("fileOpen"), iconForeground, iconAccent));
    }

    if (auto* settings = forge::core::Settings::instance()) {
        settings->setValue(QStringLiteral("ui.theme"), name);
    }
    applyWorkspaceStyle(root);
    statusBar()->showMessage(QStringLiteral("Theme: %1").arg(name), 2500);
}

void MainWindow::showSettings() {
    QDialog dialog(this);
    dialog.setWindowTitle("Settings");
    dialog.resize(760, 480);
    auto* layout = new QHBoxLayout(&dialog);
    auto* navigation = new QWidget(&dialog);
    auto* navLayout = new QVBoxLayout(navigation);
    auto* search = new QLineEdit(navigation);
    search->setPlaceholderText("Search settings…");
    auto* tree = new QTreeWidget(navigation);
    tree->setHeaderHidden(true);
    tree->setMaximumWidth(210);
    navLayout->addWidget(search);
    navLayout->addWidget(tree);
    layout->addWidget(navigation);
    auto* body = new QWidget(&dialog);
    auto* bodyLayout = new QVBoxLayout(body);
    auto* pages = new QStackedWidget(body);
    bodyLayout->addWidget(pages, 1);
    layout->addWidget(body, 1);
    auto* appearance = new QWidget(pages);
    auto* appearanceForm = new QFormLayout(appearance);
    auto* theme = new QComboBox(appearance);
    theme->addItems(forge::core::ThemeCatalog::themes().keys());
    theme->setCurrentText(m_themeName);
    appearanceForm->addRow("Color theme", theme);
    pages->addWidget(appearance);
    auto* keymapPage = new QWidget(pages);
    auto* keymapForm = new QFormLayout(keymapPage);
    auto* keymap = new QComboBox(keymapPage);
    keymap->addItems({"Default", "VS Code"});
    keymap->setCurrentIndex(
        forge::core::Settings::instance()->value("ui.keymap", "default").toString() == "vscode"
            ? 1
            : 0);
    keymapForm->addRow("Keyboard layout", keymap);
    auto* shortcuts = new QLabel(
        "Ctrl+F  Find\nCtrl+R  Replace\nCtrl+Shift+F  Find in Files\nDouble Shift / Ctrl+Shift+A  "
        "Search Everywhere\nCtrl+Shift+N  Go to File\nCtrl+J  Insert Snippet",
        keymapPage);
    keymapForm->addRow(shortcuts);
    pages->addWidget(keymapPage);
    auto* editorPage = new QWidget(pages);
    auto* editorForm = new QFormLayout(editorPage);
    auto* fontSize = new QSpinBox(editorPage);
    fontSize->setRange(8, 28);
    fontSize->setValue(forge::core::Settings::instance()->value("editor.fontSize", 10).toInt());
    auto* wrap = new QCheckBox("Wrap long lines", editorPage);
    wrap->setChecked(forge::core::Settings::instance()->value("editor.wrap", false).toBool());
    editorForm->addRow("Editor font size", fontSize);
    editorForm->addRow(wrap);
    pages->addWidget(editorPage);
    for (const auto& title : QStringList{"Appearance", "Keymap", "Editor"})
        new QTreeWidgetItem(tree, {title});
    connect(tree, &QTreeWidget::currentItemChanged, &dialog,
            [tree, pages](QTreeWidgetItem* item, QTreeWidgetItem*) {
                if (item) pages->setCurrentIndex(tree->indexOfTopLevelItem(item));
            });
    connect(search, &QLineEdit::textChanged, &dialog, [tree](const QString& term) {
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            tree->topLevelItem(i)->setHidden(
                !tree->topLevelItem(i)->text(0).contains(term, Qt::CaseInsensitive));
    });
    tree->setCurrentItem(tree->topLevelItem(0));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, body);
    bodyLayout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        auto* settings = forge::core::Settings::instance();
        settings->setValue("ui.keymap", keymap->currentIndex() == 1 ? "vscode" : "default");
        settings->setValue("editor.fontSize", fontSize->value());
        settings->setValue("editor.wrap", wrap->isChecked());
        refreshExtensionBindings();
        applyTheme(theme->currentText());
        refreshThemeMenus();
        settings->save();
    }
}

void MainWindow::showPluginManager() {
    ExtensionsDialog dialog(m_extensions, this);
    dialog.exec();
}

void MainWindow::applyEditorTheme(QPlainTextEdit* editor) {
    if (!editor) {
        return;
    }
    QFile file(themeResourcePath(m_themeName));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[MainWindow] Cannot apply editor theme:" << file.fileName();
        return;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning() << "[MainWindow] Invalid editor theme:" << error.errorString();
        return;
    }

    const QJsonObject root = document.object();
    const QJsonObject editorColors = root.value(QStringLiteral("editor")).toObject();
    const QJsonObject metrics = root.value(QStringLiteral("metrics")).toObject();
    editor->setLineWrapMode(forge::core::Settings::instance()->value("editor.wrap", false).toBool()
                                ? QPlainTextEdit::WidgetWidth
                                : QPlainTextEdit::NoWrap);
    const QFont font(
        metrics.value(QStringLiteral("fontFamily")).toString(QStringLiteral("Consolas")),
        forge::core::Settings::instance()
            ->value("editor.fontSize", metrics.value(QStringLiteral("fontSize")).toInt(10))
            .toInt());
    if (auto* codeEditor = dynamic_cast<ForgeCodeEditor*>(editor)) {
        codeEditor->setThemeFont(font);
    } else {
        editor->setFont(font);
        editor->setTabStopDistance(QFontMetrics(font).horizontalAdvance(QLatin1Char(' ')) * 4);
    }

    if (auto* codeEditor = dynamic_cast<ForgeCodeEditor*>(editor)) {
        codeEditor->setEditorColors(
            QColor(themedColor(editorColors, QStringLiteral("gutterBackground"),
                               QStringLiteral("#1E1F22"))),
            QColor(
                themedColor(editorColors, QStringLiteral("lineNumber"), QStringLiteral("#636573"))),
            QColor(themedColor(editorColors, QStringLiteral("currentLine"),
                               QStringLiteral("#26282E"))),
            QColor(themedColor(editorColors, QStringLiteral("gutterBorder"),
                               QStringLiteral("#2B2D30"))),
            QColor(themedColor(editorColors, QStringLiteral("lineNumberActive"),
                               QStringLiteral("#A8B2C1"))));
    }
    if (auto* codeEditor = dynamic_cast<ForgeCodeEditor*>(editor)) {
        if (auto* highlighter =
                dynamic_cast<ForgeSyntaxHighlighter*>(codeEditor->syntaxHighlighter())) {
            highlighter->setTheme(root.value(QStringLiteral("syntax")).toObject());
        }
    }
}

void MainWindow::applyWorkspaceStyle(const QJsonObject& theme) {
    const auto ui = theme.value("ui").toObject();
    const auto editor = theme.value("editor").toObject();
    const QString panel = ui.value("backgroundElevated").toString("#2B2D30");
    const QString foreground = ui.value("foreground").toString("#DFE1E5");
    const QString muted = ui.value("foregroundMuted").toString("#868A91");
    const QString accent = ui.value("accent").toString("#3574F0");
    const QString border = ui.value("border").toString("#393B40");
    const QString background = editor.value("background").toString("#1E1F22");
    const QString selected = editor.value("selection").toString("#2E436E");
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(background));
    palette.setColor(QPalette::WindowText, QColor(foreground));
    palette.setColor(QPalette::Base, QColor(background));
    palette.setColor(QPalette::Text, QColor(foreground));
    palette.setColor(QPalette::Button, QColor(panel));
    palette.setColor(QPalette::ButtonText, QColor(foreground));
    palette.setColor(QPalette::Highlight, QColor(selected));
    palette.setColor(QPalette::HighlightedText, QColor(foreground));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(muted));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(muted));
    qApp->setPalette(palette);
    const QString extra =
        QStringLiteral(
            "QMainWindow::separator { background:%1; width:2px; height:2px; }"
            "QToolBar#MainToolbar { min-height:36px; padding:2px 8px; background:%2; "
            "border-bottom:1px solid %1; }"
            "QToolBar#LeftStripe,QToolBar#RightStripe { background:%2; border:0; spacing:4px; "
            "padding:4px 2px; }"
            "QToolButton[stripe=true] { border:0; border-radius:6px; }"
            "QToolButton[stripe=true]:checked { background:%3; }"
            "QToolButton#ProjectSelector { font-weight:600; padding:5px 10px; }"
            "QToolButton#BranchSelector { padding:5px 8px; }"
            "QToolButton:disabled { color:%4; }"
            "QComboBox { border:0; border-radius:5px; padding:5px 10px; }"
            "QTabWidget#EditorTabs::pane { border:0; }"
            "QTabWidget#EditorTabs QTabBar::tab { min-height:20px; padding:5px 12px; border:0; "
            "border-bottom:3px solid transparent; background:%2; }"
            "QTabWidget#EditorTabs QTabBar::tab:selected { background:%5; border-bottom-color:%3; }"
            "QTabBar::close-button { subcontrol-position:right; }"
            "QPlainTextEdit#ForgeCodeEditor, QPlainTextEdit#TerminalOutput { background:%5; "
            "color:%6; border:0; selection-background-color:%7; }"
            "QPlainTextEdit#TerminalOutput { font-family:'Consolas','DejaVu Sans Mono',monospace; "
            "font-size:13px; }"
            "QTreeView#ProjectTree { background:%2; border:0; outline:0; }"
            "QTreeView#ProjectTree::item { height:22px; padding:0 3px; border-radius:3px; }"
            "QTreeView#ProjectTree::item:selected { background:%7; }"
            "QStatusBar,QStatusBar QLabel,QStatusBar QToolButton { background:%2; color:%4; "
            "border:0; font-size:12px; }"
            "QStatusBar { min-height:24px; border-top:1px solid %1; }"
            "QLabel#WelcomeTitle { font-size:34px; font-weight:600; color:%6; }"
            "QLineEdit { border:1px solid %1; border-radius:5px; padding:6px 8px; background:%5; "
            "selection-background-color:%7; }"
            "QLineEdit:focus { border-color:%3; }"
            "QDialog { border:1px solid %1; }"
            "QPushButton { border-radius:5px; }"
            "QMenu { padding:6px; border-radius:7px; background:%2; }"
            "QMenu::item { border-radius:4px; padding:6px 24px 6px 10px; }"
            "QToolButton::menu-indicator { image:none; width:0; }"
            "QWidget#FindBar { background:%2; border-bottom:1px solid %1; }")
            .arg(border, panel, accent, muted, background, foreground, selected);
    qApp->setStyleSheet(qApp->styleSheet() + extra);
    const QMap<QString, QString> icons{{"forge.window.projectTree", "project"},
                                       {"forge.navigate.findInFiles", "search"},
                                       {"forge.window.plugins", "plugins"},
                                       {"forge.navigate.searchEverywhere", "search"},
                                       {"forge.run.debug", "debug"},
                                       {"forge.debug.stepInto", "debug"},
                                       {"forge.debug.stepOver", "debug"},
                                       {"forge.debug.stepOut", "debug"}};
    for (auto it = icons.begin(); it != icons.end(); ++it)
        if (auto* action = m_actions->action(it.key()))
            action->setIcon(forgeIcon(it.value(), QColor(foreground), QColor(accent)));
    for (const auto& entry : QList<QPair<QString, QString>>{{"MainMenuButton", "menu"},
                                                            {"ProjectSelector", "project"},
                                                            {"BranchSelector", "git"},
                                                            {"SettingsMenuButton", "settings"}})
        if (auto* button = findChild<QToolButton*>(entry.first))
            button->setIcon(forgeIcon(entry.second, QColor(foreground), QColor(accent)));
    for (auto* action : m_actions->findChildren<QAction*>()) {
        const auto shortcut = action->shortcut().toString(QKeySequence::NativeText);
        action->setToolTip(action->text() +
                           (shortcut.isEmpty() ? QString() : " (" + shortcut + ')'));
    }
    for (auto* text : findChildren<QPlainTextEdit*>()) {
        QPalette p = text->palette();
        p.setColor(QPalette::Base, QColor(background));
        p.setColor(QPalette::Text, QColor(foreground));
        p.setColor(QPalette::Highlight, QColor(selected));
        text->setPalette(p);
    }
    for (auto* close : findChildren<QToolButton*>())
        if (close->property("editorTabClose").toBool())
            close->setIcon(forgeIcon("close", QColor(muted), QColor(accent)));
    updateDocumentInfo();
}
