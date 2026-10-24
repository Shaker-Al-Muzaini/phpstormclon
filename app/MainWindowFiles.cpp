#include <QCryptographicHash>
#include <QSplitter>
#include <QTimer>

#include "DocumentCodec.h"
#include "EditorWidgets.h"
#include "forge/core/FileWatcher.h"
#include "forge/core/ProjectSearch.h"
#include "MainWindow.h"
#include "TerminalView.h"
#include "WorkspaceSupport.h"

constexpr int kWindowLayoutVersion = 3;

void MainWindow::openProject() {
    const QString path =
        QFileDialog::getExistingDirectory(this, QStringLiteral("Open Project"), m_projectRoot);
    if (!path.isEmpty()) {
        setProjectRoot(path);
    }
}

void MainWindow::setProjectRoot(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir()) {
        QMessageBox::warning(this, QStringLiteral("Open Project"),
                             QStringLiteral("The selected project folder does not exist."));
        return;
    }

    if (!m_projectRoot.isEmpty())
        forge::core::Settings::instance()->setValue(
            "layout." + m_projectRoot,
            QString::fromLatin1(saveState(kWindowLayoutVersion).toBase64()));
    m_projectRoot = info.absoluteFilePath();
    m_projectSearch->setRoot(m_projectRoot);
    m_laravelLogPath =
        QDir(m_projectRoot).filePath(QStringLiteral("storage/logs/laravel.log"));
    const QString logDirectory = QFileInfo(m_laravelLogPath).absolutePath();
    m_fileWatcher->watchDirectory(logDirectory);
    m_diagnostics.clearFileSource(m_laravelLogPath, QStringLiteral("laravel.log"));
    if (QFileInfo::exists(m_laravelLogPath)) {
        m_fileWatcher->watchFile(m_laravelLogPath);
        updateLaravelLogDiagnostics();
    }
    m_projectButton->setText(info.fileName());
    refreshBranch();
    auto recent = forge::core::Settings::instance()->value("project.recent").toStringList();
    recent.removeAll(m_projectRoot);
    recent.prepend(m_projectRoot);
    while (recent.size() > 12) recent.removeLast();
    forge::core::Settings::instance()->setValue("project.recent", recent);
    const auto layout = QByteArray::fromBase64(
        forge::core::Settings::instance()->value("layout." + m_projectRoot).toString().toLatin1());
    if (!layout.isEmpty()) restoreState(layout, kWindowLayoutVersion);
    m_fileSystemModel->setRootPath(m_projectRoot);
    m_projectTree->setRootIndex(m_fileSystemModel->index(m_projectRoot));
    m_projectLabel->setText(info.fileName().isEmpty() ? m_projectRoot : info.fileName());
    setWindowTitle(
        QStringLiteral("%1 — %2").arg(info.fileName().isEmpty() ? m_projectRoot : info.fileName(),
                                      QLatin1String(FORGE_PRODUCT_NAME)));
    if (auto* settings = forge::core::Settings::instance()) {
        settings->setValue(QStringLiteral("project.lastPath"), m_projectRoot);
    }
    const QString escapedRoot =
        QString(m_projectRoot).replace(QLatin1Char('\''), QStringLiteral("''"));
#ifdef Q_OS_WIN
    const QString command = QStringLiteral("Set-Location -LiteralPath '%1'").arg(escapedRoot);
#else
    const QString command = "cd -- '" + QString(m_projectRoot).replace("'", "'\"'\"'") + "'";
    Q_UNUSED(escapedRoot);
#endif
    for (QProcess* process : std::as_const(m_terminalProcesses)) {
        if (process && process->state() != QProcess::NotRunning) {
            process->write(command.toUtf8() + "\n");
        }
    }
    statusBar()->showMessage(QStringLiteral("Project opened: %1").arg(m_projectRoot), 5000);
    findChild<QDockWidget*>("ProjectToolWindow")->show();
}

void MainWindow::openFile(const QModelIndex& index) {
    if (m_fileSystemModel->isDir(index)) {
        return;
    }
    openFilePath(m_fileSystemModel->filePath(index));
}

void MainWindow::openFilePath(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        QMessageBox::warning(this, QStringLiteral("Open File"),
                             QStringLiteral("The selected file does not exist."));
        return;
    }
    m_recentFiles.removeAll(info.absoluteFilePath());
    m_recentFiles.prepend(info.absoluteFilePath());
    while (m_recentFiles.size() > 30) {
        m_recentFiles.removeLast();
    }

    for (int index = 0; index < m_editorTabs->count(); ++index) {
        auto* editor = qobject_cast<QPlainTextEdit*>(m_editorTabs->widget(index));
        if (editor && QFileInfo(editor->property("filePath").toString()).absoluteFilePath() ==
                          info.absoluteFilePath()) {
            m_editorTabs->setCurrentIndex(index);
            m_centerStack->setCurrentWidget(m_editorSplitter);
            return;
        }
    }

    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(
            this, QStringLiteral("Open File"),
            QStringLiteral("Could not read %1:\n%2").arg(info.fileName(), file.errorString()));
        return;
    }
    if (file.size() > 64 * 1024 * 1024) {
        QMessageBox::warning(
            this, "Large File",
            "Files larger than 64 MiB are not opened to keep the workspace responsive.");
        return;
    }
    const QByteArray contents = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        QMessageBox::critical(this, QStringLiteral("Open File"),
                              QStringLiteral("Could not read %1 completely:\n%2")
                                  .arg(info.fileName(), file.errorString()));
        return;
    }
    forge::core::DecodedText decoded;
    QString decodeError;
    if (!forge::core::TextFileCodec::decode(contents, &decoded, &decodeError)) {
        QMessageBox::critical(
            this, QStringLiteral("Open File"),
            QStringLiteral("Could not decode %1:\n%2").arg(info.fileName(), decodeError));
        return;
    }
    if (decoded.text.contains(QChar::Null)) {
        QMessageBox::warning(
            this, QStringLiteral("Open File"),
            QStringLiteral("%1 appears to be a binary file.").arg(info.fileName()));
        return;
    }

    auto* editor = new ForgeCodeEditor(m_editorTabs);
    editor->setProperty("filePath", info.absoluteFilePath());
    editor->setProperty("forgeSourceEditor", true);
    editor->setProperty("indentSize", 4);
    editor->setProperty("textEncoding", static_cast<int>(decoded.encoding));
    editor->setObjectName(QStringLiteral("ForgeCodeEditor"));
    editor->setProperty("sourceHash",
                        QCryptographicHash::hash(contents, QCryptographicHash::Sha256));
    m_fileWatcher->watchFile(info.absoluteFilePath());
    editor->setPlainText(decoded.text);
    storeLineEndings(editor->document(), decoded.lineEndings);
    auto* highlighter = new ForgeSyntaxHighlighter(
        editor->document(),
        info.fileName().toLower().endsWith(QStringLiteral(".blade.php")) ? QStringLiteral("blade")
                                                                         : info.suffix().toLower(),
        editor);
    editor->setSyntaxHighlighter(highlighter);
    if (decoded.text.size() > 200000) {
        highlighter->setDocument(nullptr);
        statusBar()->showMessage("Large file mode: syntax highlighting and folding disabled.",
                                 6000);
    }
    applyEditorTheme(editor);
    editor->document()->setModified(false);
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this,
            [this]() { updateEditorStatus(); });
    const int tabIndex =
        m_editorTabs->addTab(editor, m_fileIconProvider->icon(info), info.fileName());
    decorateEditorTab(editor);
    m_editorTabs->setTabToolTip(
        tabIndex,
        QStringLiteral("%1\nLanguage: %2").arg(info.absoluteFilePath(), fileLanguageName(info)));
    m_editorTabs->setCurrentIndex(tabIndex);
    connect(
        editor->document(), &QTextDocument::modificationChanged, this,
        [this, editor](bool modified) {
            const int index = m_editorTabs->indexOf(editor);
            if (index >= 0) {
                const QString title = QFileInfo(editor->property("filePath").toString()).fileName();
                m_editorTabs->setTabText(index, modified ? title + QStringLiteral(" •") : title);
                updateDocumentInfo();
            }
        });
    m_centerStack->setCurrentWidget(m_editorSplitter);
    connectPhpIntelDocument(editor);
    updateDiagnostics();
    updateProblemsCurrentDocument();
    const auto treeIndex = m_fileSystemModel->index(info.absoluteFilePath());
    m_projectTree->setCurrentIndex(treeIndex);
    m_projectTree->scrollTo(treeIndex);
}

void MainWindow::createUntitledDocument() {
    auto* editor = new ForgeCodeEditor(m_editorTabs);
    editor->setProperty("filePath", QString());
    editor->setProperty("forgeSourceEditor", true);
    editor->setProperty("indentSize", 4);
    editor->setProperty("textEncoding", static_cast<int>(forge::core::TextEncoding::Utf8));
    editor->setObjectName(QStringLiteral("ForgeCodeEditor"));
    auto* highlighter = new ForgeSyntaxHighlighter(editor->document(), QString(), editor);
    editor->setSyntaxHighlighter(highlighter);
    applyEditorTheme(editor);
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this,
            [this]() { updateEditorStatus(); });
    connect(editor->document(), &QTextDocument::modificationChanged, this,
            [this, editor](bool modified) {
                const int tab = m_editorTabs->indexOf(editor);
                if (tab >= 0)
                    m_editorTabs->setTabText(
                        tab, (editor->property("filePath").toString().isEmpty()
                                  ? QString("Untitled")
                                  : QFileInfo(editor->property("filePath").toString()).fileName()) +
                                 (modified ? " •" : ""));
                updateDocumentInfo();
            });
    const int index = m_editorTabs->addTab(editor, QStringLiteral("Untitled"));
    decorateEditorTab(editor);
    m_editorTabs->setCurrentIndex(index);
    m_centerStack->setCurrentWidget(m_editorSplitter);
    editor->setFocus();
    updateProblemsCurrentDocument();
}

bool MainWindow::saveCurrentDocument() {
    QPlainTextEdit* editor = currentEditor();
    if (!editor) {
        statusBar()->showMessage(QStringLiteral("There is no open document to save."), 4000);
        return false;
    }
    return saveDocument(editor);
}

bool MainWindow::saveDocument(QPlainTextEdit* editor) {
    QString path = editor->property("filePath").toString();
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, QStringLiteral("Save File"), m_projectRoot);
        if (path.isEmpty()) {
            return false;
        }
    }

    if (!confirmExternalChange(editor, path)) return false;
    const auto encoding =
        static_cast<forge::core::TextEncoding>(editor->property("textEncoding").toInt());
    QByteArray serializedContents;
    QString encodeError;
    if (!forge::core::TextFileCodec::encode(editor->toPlainText(), encoding,
                                            documentLineEndings(editor->document()),
                                            &serializedContents, &encodeError)) {
        QMessageBox::critical(this, QStringLiteral("Save File"),
                              QStringLiteral("Could not encode %1:\n%2").arg(path, encodeError));
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(
            this, QStringLiteral("Save File"),
            QStringLiteral("Could not save %1:\n%2").arg(path, file.errorString()));
        return false;
    }
    if (file.write(serializedContents) != serializedContents.size()) {
        const QString fileError = file.errorString();
        file.cancelWriting();
        QMessageBox::critical(
            this, QStringLiteral("Save File"),
            QStringLiteral("The file could not be written completely:\n%1").arg(fileError));
        return false;
    }
    if (!file.commit()) {
        QMessageBox::critical(
            this, QStringLiteral("Save File"),
            QStringLiteral("Could not complete saving %1:\n%2").arg(path, file.errorString()));
        return false;
    }

    editor->setProperty("filePath", path);
    editor->setProperty("sourceHash",
                        QCryptographicHash::hash(serializedContents, QCryptographicHash::Sha256));
    editor->setProperty("externalChange", false);
    m_fileWatcher->watchFile(path);
    for (auto* view : findChildren<QPlainTextEdit*>()) {
        if (view->document() == editor->document()) {
            view->setProperty("filePath", path);
            view->setProperty("sourceHash", editor->property("sourceHash"));
            view->setProperty("externalChange", false);
            view->setProperty("textEncoding", editor->property("textEncoding"));
        }
    }
    editor->document()->setModified(false);
    connectPhpIntelDocument(editor);
    runPhpSyntaxCheck(path);
    updateDocumentInfo();
    updateDiagnostics();
    int index = m_editorTabs->indexOf(editor);
    if (index < 0 && m_splitEditor == editor) index = m_editorTabs->currentIndex();
    if (index >= 0) {
        const QFileInfo info(path);
        m_editorTabs->setTabText(index, info.fileName());
        m_editorTabs->setTabToolTip(index, info.absoluteFilePath());
    }
    statusBar()->showMessage(QStringLiteral("Saved %1").arg(path), 3000);
    return true;
}

void MainWindow::closeDocument(int index) {
    auto* editor = qobject_cast<QPlainTextEdit*>(m_editorTabs->widget(index));
    if (!editor) {
        return;
    }
    const QString path = editor->property("filePath").toString();
    if (!path.isEmpty()) {
        if (m_phpIntelClient && editor->property("phpIntelOpened").toBool()) {
            if (QProcess* process = m_supervisor->process(QStringLiteral("phpintel"));
                process && process->state() == QProcess::Running) {
                m_phpIntelClient->sendNotification(
                    QStringLiteral("textDocument/didClose"),
                    {{QStringLiteral("textDocument"),
                      QJsonObject{{QStringLiteral("uri"), QUrl::fromLocalFile(path).toString(
                                                             QUrl::FullyEncoded)}}}});
            }
        }
        m_diagnostics.clearFileSource(path, QStringLiteral("phpintel"));
    }
    if (editor->document()->isModified()) {
        const QMessageBox::StandardButton result = QMessageBox::warning(
            this, QStringLiteral("Unsaved changes"),
            QStringLiteral("Save changes to %1?")
                .arg(m_editorTabs->tabText(index).remove(QLatin1Char('*'))),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (result == QMessageBox::Cancel ||
            (result == QMessageBox::Save && !saveDocument(editor))) {
            return;
        }
    }
    QWidget* removed = m_editorTabs->widget(index);
    m_editorTabs->removeTab(index);
    removed->deleteLater();
    if (m_editorTabs->count() == 0) {
        m_centerStack->setCurrentWidget(m_welcomePage);
    }
    updateDiagnostics();
}

QPlainTextEdit* MainWindow::currentEditor() const {
    auto* focused = qobject_cast<QPlainTextEdit*>(QApplication::focusWidget());
    if (focused == m_splitEditor && focused && focused->property("forgeSourceEditor").toBool())
        return focused;
    return qobject_cast<QPlainTextEdit*>(m_editorTabs->currentWidget());
}

void MainWindow::updateEditorStatus() {
    QPlainTextEdit* editor = currentEditor();
    updateDocumentInfo();
    if (!editor) {
        for (const auto& id :
             {"forge.run.run", "forge.run.debug", "forge.run.profile", "forge.run.stop"})
            m_actions->action(id)->setEnabled(false);
        m_caretLabel->setText(QStringLiteral("Ln 1, Col 1"));
        m_fileTypeLabel->setText(QStringLiteral("No file"));
        return;
    }

    const QTextCursor cursor = editor->textCursor();
    m_caretLabel->setText(QStringLiteral("Ln %1, Col %2")
                              .arg(cursor.blockNumber() + 1)
                              .arg(cursor.positionInBlock() + 1));
    const QString path = editor->property("filePath").toString();
    m_fileTypeLabel->setText(path.isEmpty() ? QStringLiteral("Plain Text")
                                            : fileLanguageName(QFileInfo(path)));
}

void MainWindow::decorateEditorTab(QPlainTextEdit* editor) {
    const int index = m_editorTabs->indexOf(editor);
    if (index < 0) return;
    auto* close = new QToolButton(m_editorTabs->tabBar());
    close->setProperty("editorTabClose", true);
    close->setAutoRaise(true);
    close->setFixedSize(18, 18);
    close->setIconSize(QSize(12, 12));
    close->setToolTip("Close Tab");
    close->setIcon(forgeIcon("close", palette().color(QPalette::WindowText),
                             palette().color(QPalette::Highlight)));
    close->setStyleSheet(
        "QToolButton { background:transparent; border:0; padding:2px; } QToolButton:hover { "
        "background:palette(mid); border-radius:3px; }");
    m_editorTabs->tabBar()->setTabButton(index, QTabBar::RightSide, close);
    connect(close, &QToolButton::clicked, this, [this, editor]() {
        const int tab = m_editorTabs->indexOf(editor);
        if (tab >= 0) closeDocument(tab);
    });
}
