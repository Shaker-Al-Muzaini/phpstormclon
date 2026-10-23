#include <QClipboard>
#include <QCryptographicHash>
#include <QKeyEvent>
#include <QPlainTextDocumentLayout>
#include <QSplitter>
#include <QTimer>

#include "DocumentCodec.h"
#include "EditorWidgets.h"
#include "FindBar.h"
#include "forge/core/ExtensionManager.h"
#include "forge/core/FileWatcher.h"
#include "forge/core/ProjectSearch.h"
#include "forge/core/ThemeCatalog.h"
#include "MainWindow.h"
#include "WorkspaceSupport.h"

void MainWindow::setupWorkspaceFeatures() {
    connect(m_extensions, &forge::core::ExtensionManager::changed, this, [this]() {
        refreshExtensionBindings();
        refreshThemeMenus();
        if (!forge::core::ThemeCatalog::themes().contains(m_themeName)) m_themeName = "Graphite";
        applyTheme(m_themeName);
    });
    connect(m_fileWatcher, &forge::core::FileWatcher::fileChanged, this,
            [this](const QString& path) {
                QFile file(path);
                QByteArray hash;
                if (file.open(QIODevice::ReadOnly))
                    hash = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
                for (int i = 0; i < m_editorTabs->count(); ++i) {
                    auto* editor = qobject_cast<QPlainTextEdit*>(m_editorTabs->widget(i));
                    if (editor && editor->property("filePath").toString() == path &&
                        editor->property("sourceHash").toByteArray() != hash) {
                        editor->setProperty("externalChange", true);
                        statusBar()->showMessage(
                            "File changed outside Forge: " + QFileInfo(path).fileName() +
                                ". Saving will ask before overwriting.",
                            10000);
                    }
                }
            });
    connect(m_projectSearch, &forge::core::ProjectSearch::filesReady, this, [this]() {
        statusBar()->showMessage(QStringLiteral("%1 project files ready for search")
                                     .arg(m_projectSearch->files().size()),
                                 2500);
    });
}
void MainWindow::refreshExtensionBindings() {
    if (!m_keymapManager) return;
    for (const auto& id : m_actions->allActionIds()) m_actions->action(id)->setShortcut({});
    const auto keymap = forge::core::Settings::instance()->value("ui.keymap", "default").toString();
    m_keymapManager->loadKeymap(":/keymaps/" + keymap + ".json");
    const auto bindings = m_extensions->keybindings();
    QHash<QString, QString> mapped;
    for (auto it = bindings.begin(); it != bindings.end(); ++it)
        mapped.insert(it.key(), it.value().toString());
    m_actions->applyKeymap(mapped);
    setupShortcuts();
}
bool MainWindow::eventFilter(QObject* object, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Shift && !key->isAutoRepeat())
            m_shiftOnly = true;
        else if (key->key() != Qt::Key_Shift)
            m_shiftOnly = false;
    } else if (event->type() == QEvent::KeyRelease) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Shift && m_shiftOnly && !key->isAutoRepeat() &&
            !QApplication::activeModalWidget()) {
            if (m_shiftTimer.isValid() && m_shiftTimer.elapsed() < 450) {
                m_shiftTimer.invalidate();
                showQuickOpen();
            } else
                m_shiftTimer.start();
            m_shiftOnly = false;
        }
    }
    return QMainWindow::eventFilter(object, event);
}
void MainWindow::reloadDocument(QPlainTextEdit* editor) {
    QFile file(editor->property("filePath").toString());
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Reload", file.errorString());
        return;
    }
    const auto bytes = file.readAll();
    forge::core::DecodedText decoded;
    QString error;
    if (!forge::core::TextFileCodec::decode(bytes, &decoded, &error)) {
        QMessageBox::warning(this, "Reload", error);
        return;
    }
    const auto position = editor->textCursor().position();
    editor->setPlainText(decoded.text);
    storeLineEndings(editor->document(), decoded.lineEndings);
    editor->setProperty("textEncoding", static_cast<int>(decoded.encoding));
    editor->setProperty("sourceHash", QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
    editor->setProperty("externalChange", false);
    editor->document()->setModified(false);
    auto cursor = editor->textCursor();
    cursor.setPosition(qMin(position, editor->document()->characterCount() - 1));
    editor->setTextCursor(cursor);
    updateDocumentInfo();
}
bool MainWindow::confirmExternalChange(QPlainTextEdit* editor, const QString& path) {
    const auto original = editor->property("sourceHash");
    if (!original.isValid()) return true;
    QFile file(path);
    QByteArray hash;
    if (file.open(QIODevice::ReadOnly))
        hash = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
    if (hash == original.toByteArray()) return true;
    QMessageBox dialog(QMessageBox::Warning, "File Changed on Disk",
                       "This file was changed or removed outside Forge. Choose how to handle the "
                       "external version.",
                       QMessageBox::NoButton, this);
    auto* overwrite = dialog.addButton("Overwrite Disk Version", QMessageBox::DestructiveRole);
    auto* reload = dialog.addButton("Reload from Disk", QMessageBox::AcceptRole);
    dialog.addButton(QMessageBox::Cancel);
    dialog.setDefaultButton(QMessageBox::Cancel);
    dialog.exec();
    if (dialog.clickedButton() == reload) reloadDocument(editor);
    return dialog.clickedButton() == overwrite;
}
void MainWindow::updateDocumentInfo() {
    if (!m_encodingButton) return;
    auto* editor = currentEditor();
    for (auto* button : {m_encodingButton, m_eolButton, m_indentButton, m_readOnlyButton})
        button->setEnabled(editor != nullptr);
    if (!editor) return;
    m_encodingButton->setText(forge::core::TextFileCodec::encodingName(
        static_cast<forge::core::TextEncoding>(editor->property("textEncoding").toInt())));
    const auto endings = documentLineEndings(editor->document());
    const bool mixed = !endings.isEmpty() && std::any_of(endings.begin(), endings.end(),
                                                         [first = endings.first()](auto value) {
                                                             return value != first;
                                                         });
    m_eolButton->setText(
        mixed ? "Mixed"
              : forge::core::TextFileCodec::lineEndingName(
                    endings.isEmpty() ? forge::core::LineEnding::Lf : endings.first()));
    const int indent =
        editor->property("indentSize").isValid() ? editor->property("indentSize").toInt() : 4;
    m_indentButton->setText(QStringLiteral("%1 spaces").arg(indent));
    m_readOnlyButton->setText(editor->isReadOnly() ? "RO" : "RW");
    const auto path = editor->property("filePath").toString();
    QString relative = m_projectRoot.isEmpty() ? path : QDir(m_projectRoot).relativeFilePath(path);
    relative.replace('/', "  ›  ");
    m_breadcrumbs->setText(relative.isEmpty() ? "Untitled" : relative);
    m_breadcrumbs->setToolTip(path);
    const bool php =
        !path.isEmpty() && QFileInfo(path).suffix().compare("php", Qt::CaseInsensitive) == 0;
    for (const auto& id : {"forge.run.run", "forge.run.debug", "forge.run.profile"})
        m_actions->action(id)->setEnabled(php && !editor->document()->isModified());
    const bool running = m_commandProcess && m_commandProcess->state() != QProcess::NotRunning;
    m_actions->action("forge.run.stop")->setEnabled(running);
    for (const auto& id : {"forge.debug.stepOver", "forge.debug.stepInto", "forge.debug.stepOut"})
        m_actions->action(id)->setEnabled(m_debugSocket && m_debugSocket->state() ==
                                                               QAbstractSocket::ConnectedState);
}
void MainWindow::refreshBranch() {
    if (!m_branchButton) return;
    const auto root = m_projectRoot;
    auto* process = new QProcess(this);
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, root](int code, QProcess::ExitStatus) {
                if (root == m_projectRoot) {
                    const auto branch =
                        QString::fromUtf8(process->readAllStandardOutput()).trimmed();
                    m_branchButton->setText(
                        code == 0 && !branch.isEmpty()
                            ? m_branchButton->fontMetrics().elidedText(branch, Qt::ElideRight, 165)
                            : "Git");
                    m_branchButton->setToolTip("Show Git status: " + branch);
                }
                process->deleteLater();
            });
    connect(process, &QProcess::errorOccurred, process, [process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) process->deleteLater();
    });
    process->setWorkingDirectory(root);
    process->start("git", {"branch", "--show-current"});
}
void MainWindow::insertSnippet() {
    auto* editor = currentEditor();
    if (!editor || editor->isReadOnly()) return;
    const auto snippets = m_extensions->snippets();
    if (snippets.isEmpty()) {
        QMessageBox::information(this, "Snippets",
                                 "Install a snippet extension from Settings → Extensions first.");
        return;
    }
    QStringList names;
    for (const auto& item : snippets) names.append(item.toObject().value("name").toString());
    bool ok = false;
    const auto name =
        QInputDialog::getItem(this, "Insert Snippet", "Snippet", names, 0, false, &ok);
    if (!ok) return;
    const auto snippet = snippets.at(names.indexOf(name)).toObject();
    QString body;
    if (snippet.value("body").isArray()) {
        QStringList lines;
        for (const auto& line : snippet.value("body").toArray()) lines.append(line.toString());
        body = lines.join('\n');
    } else
        body = snippet.value("body").toString();
    body.replace(QRegularExpression("\\$\\{\\d+:([^}]*)\\}"), "\\1");
    body.remove(QRegularExpression("\\$\\{?\\d+\\}?"));
    editor->textCursor().insertText(body);
}
void MainWindow::editLine(const QString& operation) {
    auto* editor = currentEditor();
    if (!editor || editor->isReadOnly()) return;
    QTextCursor cursor = editor->textCursor();
    cursor.beginEditBlock();
    const auto block = cursor.block();
    const QString text = block.text();
    if (operation == "duplicateLine") {
        cursor.movePosition(QTextCursor::EndOfBlock);
        cursor.insertText('\n' + text);
    } else if (operation == "deleteLine") {
        cursor.select(QTextCursor::BlockUnderCursor);
        cursor.removeSelectedText();
        if (cursor.atStart()) cursor.deleteChar();
    } else if (operation == "toggleComment") {
        const int start = editor->document()->findBlock(cursor.selectionStart()).blockNumber();
        int end = editor->document()->findBlock(cursor.selectionEnd()).blockNumber();
        if (cursor.hasSelection() &&
            cursor.selectionEnd() ==
                editor->document()->findBlock(cursor.selectionEnd()).position())
            --end;
        const auto suffix = QFileInfo(editor->property("filePath").toString()).suffix();
        const QString marker = suffix == "py" || suffix == "yaml" || suffix == "yml" ? "#" : "//";
        for (int line = qMax(start, end); line >= start; --line) {
            auto b = editor->document()->findBlockByNumber(line);
            const auto t = b.text();
            const int offset = static_cast<int>(t.size() - t.trimmed().size());
            const int pos = static_cast<int>(t.indexOf(QRegularExpression("\\S")));
            if (pos < 0) continue;
            cursor.setPosition(b.position() + pos);
            if (t.mid(pos).startsWith(marker)) {
                for (int i = 0; i < marker.size(); ++i) cursor.deleteChar();
                if (cursor.document()->characterAt(cursor.position()) == ' ') cursor.deleteChar();
            } else
                cursor.insertText(marker + ' ');
            Q_UNUSED(offset);
        }
    } else {
        const auto adjacent = operation == "moveLineUp" ? block.previous() : block.next();
        if (adjacent.isValid()) {
            const int first = qMin(block.position(), adjacent.position());
            const int end =
                qMax(block.position() + block.length(), adjacent.position() + adjacent.length()) -
                1;
            cursor.setPosition(first);
            cursor.setPosition(end, QTextCursor::KeepAnchor);
            cursor.insertText(operation == "moveLineUp" ? text + '\n' + adjacent.text()
                                                        : adjacent.text() + '\n' + text);
        }
    }
    cursor.endEditBlock();
    editor->setTextCursor(cursor);
}
void MainWindow::projectContextMenu(const QPoint& position) {
    const auto index = m_projectTree->indexAt(position);
    const auto selected = index.isValid() ? m_fileSystemModel->filePath(index) : m_projectRoot;
    if (selected.isEmpty()) return;
    const QFileInfo info(selected);
    const auto directory = info.isDir() ? selected : info.absolutePath();
    QMenu menu(this);
    auto* file = menu.addAction("New File…");
    auto* folder = menu.addAction("New Folder…");
    auto* rename = menu.addAction("Rename…");
    auto* trash = menu.addAction("Move to Recycle Bin…");
    menu.addSeparator();
    auto* copy = menu.addAction("Copy Path");
    auto* reveal = menu.addAction("Show in File Manager");
    const auto* action = menu.exec(m_projectTree->viewport()->mapToGlobal(position));
    if (action == copy) QApplication::clipboard()->setText(selected);
    if (action == reveal) QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
    if (action == file || action == folder || action == rename) {
        bool ok = false;
        const auto name =
            QInputDialog::getText(this, action->text(), "Name", QLineEdit::Normal,
                                  action == rename ? info.fileName() : QString(), &ok);
        if (!ok || name.isEmpty() || name == "." || name == ".." || name.contains('/') ||
            name.contains('\\') || name.contains(':'))
            return;
        const auto target = QDir(action == rename ? info.absolutePath() : directory).filePath(name);
        bool success = false;
        if (action == rename)
            success = QDir().rename(selected, target);
        else if (action == folder)
            success = QDir().mkdir(target);
        else {
            QFile created(target);
            success = created.open(QIODevice::WriteOnly | QIODevice::NewOnly);
            if (success) {
                created.close();
                openFilePath(target);
            }
        }
        if (!success)
            QMessageBox::warning(this, "Project",
                                 "The operation could not be completed. The path may already exist "
                                 "or be read-only.");
    }
    if (action == trash &&
        QMessageBox::question(this, "Recycle Bin",
                              "Move this path to the recycle bin?\n" + selected) ==
            QMessageBox::Yes &&
        !QFile::moveToTrash(selected))
        QMessageBox::warning(this, "Recycle Bin", "Could not move this path to the recycle bin.");
    m_projectSearch->setRoot(m_projectRoot);
}

void MainWindow::splitEditor(Qt::Orientation orientation) {
    if (!m_editorTabs->currentWidget()) return;
    if (!m_splitEditor) {
        m_splitEditor = new ForgeCodeEditor(m_editorSplitter);
        m_splitEditor->setObjectName("ForgeCodeEditor");
        m_splitEditor->setProperty("forgeSourceEditor", true);
        m_editorSplitter->addWidget(m_splitEditor);
        connect(m_splitEditor, &QPlainTextEdit::cursorPositionChanged, this,
                [this]() { updateEditorStatus(); });
    }
    m_editorSplitter->setOrientation(orientation);
    syncSplitEditor();
    m_splitEditor->show();
    m_editorSplitter->setSizes({1, 1});
    m_splitEditor->setFocus();
}
void MainWindow::syncSplitEditor() {
    if (!m_splitEditor) return;
    auto* source = qobject_cast<QPlainTextEdit*>(m_editorTabs->currentWidget());
    if (!source) {
        auto* empty = new QTextDocument(m_splitEditor);
        empty->setDocumentLayout(new QPlainTextDocumentLayout(empty));
        m_splitEditor->setDocument(empty);
        m_splitEditor->hide();
        return;
    }
    m_splitEditor->setDocument(source->document());
    for (const auto& key :
         {"filePath", "sourceHash", "textEncoding", "indentSize", "externalChange"})
        m_splitEditor->setProperty(key, source->property(key));
    m_splitEditor->setReadOnly(source->isReadOnly());
    applyEditorTheme(m_splitEditor);
}
