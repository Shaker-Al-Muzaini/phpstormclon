#include "PhpEditorController.h"

#include <algorithm>
#include <QAbstractItemView>
#include <QApplication>
#include <QCompleter>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>
#include <QTextBlock>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "forge/core/DiagnosticsModel.h"
#include "forge/core/Settings.h"

namespace forge::editor {
int position(QTextDocument* document, const QJsonObject& point) {
    const int line = point.value("line").toInt(-1);
    const int column = point.value("character").toInt(-1);
    if (!document || line < 0 || column < 0) return -1;
    const auto block = document->findBlockByNumber(line);
    if (!block.isValid() || column > block.text().size()) return -1;
    return block.position() + column;
}
bool applyEdits(QTextDocument* document, const QJsonArray& edits) {
    struct Edit {
        int start;
        int end;
        QString text;
    };
    QList<Edit> changes;
    for (const auto& value : edits) {
        const auto object = value.toObject();
        const auto range = object.value("range").toObject();
        const int start = position(document, range.value("start").toObject());
        const int end = position(document, range.value("end").toObject());
        if (start < 0 || end < start || !object.value("newText").isString()) return false;
        changes.append({start, end, object.value("newText").toString()});
    }
    std::sort(changes.begin(), changes.end(),
              [](const auto& a, const auto& b) { return a.start < b.start; });
    for (int i = 1; i < changes.size(); ++i)
        if (changes.at(i).start < changes.at(i - 1).end ||
            changes.at(i).start == changes.at(i - 1).start)
            return false;
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    for (auto edit = changes.crbegin(); edit != changes.crend(); ++edit) {
        cursor.setPosition(edit->start);
        cursor.setPosition(edit->end, QTextCursor::KeepAnchor);
        cursor.insertText(edit->text);
    }
    cursor.endEditBlock();
    return true;
}
}  // namespace forge::editor

namespace {
bool phpFile(const QString& path) {
    return path.endsWith(".php", Qt::CaseInsensitive) &&
           !path.endsWith(".blade.php", Qt::CaseInsensitive);
}
QJsonObject point(QTextCursor cursor) {
    return {{"line", cursor.blockNumber()}, {"character", cursor.positionInBlock()}};
}
QString defaultScript() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
           "/language-servers/node_modules/intelephense/lib/intelephense.js";
}
}  // namespace
PhpEditorController::PhpEditorController(QWidget* window,
                                         forge::core::DiagnosticsModel* diagnostics)
    : QObject(window),
      m_window(window),
      m_diagnostics(diagnostics),
      m_client(this),
      m_completer(new QCompleter(this)),
      m_model(new QStandardItemModel(this)) {
    m_completer->setObjectName("PhpCompleter");
    m_completer->setModel(m_model);
    m_completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    connect(m_completer, qOverload<const QModelIndex&>(&QCompleter::activated), this,
            &PhpEditorController::acceptCompletion);
    connect(&m_client, &forge::core::PhpLanguageClient::stateChanged, this,
            [this](bool ready, const QString& status) {
                if (ready)
                    m_diagnostics->clearSource("phpintel");
                else
                    m_diagnostics->clearSource("PHP language server");
                if (message) message(status);
            });
    connect(&m_client, &forge::core::PhpLanguageClient::diagnosticsReceived, this,
            &PhpEditorController::diagnostics);
    auto* scanTimer = new QTimer(this);
    scanTimer->setInterval(4000);
    connect(scanTimer, &QTimer::timeout, this, &PhpEditorController::scanWorkspace);
    scanTimer->start();
    qApp->installEventFilter(this);
}
PhpEditorController::~PhpEditorController() {
    qApp->removeEventFilter(this);
    m_completer->setWidget(nullptr);
    m_client.disconnect(this);
}
QPlainTextEdit* PhpEditorController::editorFor(QTextDocument* document) const {
    for (auto* editor : m_window->findChildren<QPlainTextEdit*>())
        if (editor->property("forgeSourceEditor").toBool() && editor->document() == document)
            return editor;
    return nullptr;
}
void PhpEditorController::setWorkspace(const QString& workspace, bool clearCache) {
    if (m_workspace != workspace) m_diskFiles.clear();
    m_workspace = workspace;
    const auto* settings = forge::core::Settings::instance();
    forge::core::PhpLanguageClient::Configuration config;
    config.node =
        settings->value("php.language.node", QStandardPaths::findExecutable("node")).toString();
    config.script = settings->value("php.language.script", defaultScript()).toString();
    config.phpVersion = settings->value("php.language.version", "8.3.0").toString();
    config.workspace = workspace;
    config.storage =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/php/" +
        QString::fromLatin1(QCryptographicHash::hash(workspace.toUtf8(), QCryptographicHash::Sha256)
                                .toHex()
                                .left(24));
    config.clearCache = clearCache;
    if (workspace.isEmpty() || !QFileInfo::exists(config.script) || config.node.isEmpty()) {
        m_client.stop();
        if (message)
            message(
                "PHP intelligence: install or configure the local server in Tools > PHP "
                "Intelligence.");
        return;
    }
    QDir().mkpath(config.storage);
    m_diagnostics->clearSource("PHP language server");
    m_client.start(config);
    for (auto it = m_documents.begin(); it != m_documents.end(); ++it)
        if (auto* editor = editorFor(it.key())) sync(editor);
}
void PhpEditorController::attach(QPlainTextEdit* editor) {
    if (!editor || !editor->property("forgeSourceEditor").toBool()) return;
    const auto path = editor->property("filePath").toString();
    auto* document = editor->document();
    if (m_documents.contains(document) && m_documents.value(document).path != path)
        closeDocument(editor);
    if (!phpFile(path)) return;
    if (!m_documents.contains(document)) {
        m_documents.insert(document, {document, path, -1, 0});
        connect(document, &QTextDocument::contentsChange, this,
                [this, document](int, int removed, int added) {
                    if (!removed && !added) return;
                    const auto found = m_documents.constFind(document);
                    if (found == m_documents.cend()) return;
                    m_diagnostics->clearFileSource(found->path, "PHP language server");
                    if (auto* active = editorFor(document)) sync(active);
                    m_completer->popup()->hide();
                });
        connect(document, &QObject::destroyed, this, [this, document]() {
            const auto data = m_documents.take(document);
            if (!data.path.isEmpty()) m_client.closeDocument(data.path);
        });
    }
    sync(editor);
}
void PhpEditorController::sync(QPlainTextEdit* editor) {
    auto it = m_documents.find(editor->document());
    if (it == m_documents.end() || it->revision == editor->document()->revision()) return;
    it->revision = editor->document()->revision();
    m_client.setDocument(it->path, editor->toPlainText(), ++it->version);
}
void PhpEditorController::closeDocument(QPlainTextEdit* editor) {
    const auto data = m_documents.take(editor->document());
    if (data.path.isEmpty()) return;
    editor->document()->disconnect(this);
    m_client.closeDocument(data.path);
    m_diagnostics->clearFileSource(data.path, "PHP language server");
}
QJsonObject PhpEditorController::parameters(QPlainTextEdit* editor) {
    attach(editor);
    return {{"textDocument", QJsonObject{{"uri", forge::core::PhpLanguageClient::uriForFile(
                                                     editor->property("filePath").toString())}}},
            {"position", point(editor->textCursor())}};
}
void PhpEditorController::complete(QPlainTextEdit* editor) {
    if (!editor || !phpFile(editor->property("filePath").toString()) || !isReady()) return;
    m_client.cancel(m_completionRequest);
    const auto params = parameters(editor);
    const QPointer<QPlainTextEdit> target(editor);
    const int revision = editor->document()->revision();
    const int caret = editor->textCursor().position();
    const auto text = editor->toPlainText();
    int start = caret;
    while (start > 0 && (text.at(start - 1).isLetterOrNumber() || text.at(start - 1) == '_' ||
                         text.at(start - 1) == '$'))
        --start;
    m_completionRequest = m_client.request(
        "textDocument/completion", params,
        [this, target, revision, caret, start](const QJsonValue& result, const QJsonObject& error) {
            if (!target || target->document()->revision() != revision ||
                target->textCursor().position() != caret || !error.isEmpty())
                return;
            const auto items =
                result.isArray() ? result.toArray() : result.toObject().value("items").toArray();
            m_model->clear();
            for (const auto& value : items) {
                const auto item = value.toObject();
                if (item.value("label").toString().isEmpty()) continue;
                auto* row = new QStandardItem(item.value("label").toString());
                row->setData(item, Qt::UserRole);
                row->setToolTip(item.value("detail").toString());
                m_model->appendRow(row);
                if (m_model->rowCount() >= 250) break;
            }
            if (!m_model->rowCount()) return;
            m_completionEditor = target;
            m_completionRevision = revision;
            m_completionPosition = caret;
            m_completionStart = start;
            m_completer->setWidget(target);
            m_completer->setCompletionPrefix({});
            auto rectangle = target->cursorRect();
            rectangle.setWidth(
                qMin(600, qMax(320, m_completer->popup()->sizeHintForColumn(0) + 30)));
            m_completer->complete(rectangle);
            m_completer->popup()->setCurrentIndex(m_completer->completionModel()->index(0, 0));
        });
}
void PhpEditorController::acceptCompletion(const QModelIndex& index) {
    auto* editor = m_completionEditor.data();
    if (!editor || editor->document()->revision() != m_completionRevision ||
        editor->textCursor().position() != m_completionPosition)
        return;
    auto item = index.data(Qt::UserRole).toJsonObject();
    QPointer<QPlainTextEdit> target(editor);
    auto apply = [this, target, revision = m_completionRevision, caret = m_completionPosition,
                  start = m_completionStart](const QJsonObject& completion) {
        if (!target || target->document()->revision() != revision ||
            target->textCursor().position() != caret)
            return;
        auto edit = completion.value("textEdit").toObject();
        if (!edit.contains("range") && edit.contains("replace"))
            edit.insert("range", edit.value("replace"));
        if (edit.isEmpty()) {
            auto cursor = target->textCursor();
            cursor.setPosition(start);
            const auto begin = point(cursor);
            cursor.setPosition(caret);
            edit = {
                {"range", QJsonObject{{"start", begin}, {"end", point(cursor)}}},
                {"newText",
                 completion.value("insertText").toString(completion.value("label").toString())}};
        }
        if (completion.value("insertTextFormat").toInt(1) == 2) {
            if (message)
                message("The server returned an unsupported snippet; no text was changed.");
            return;
        }
        auto edits = completion.value("additionalTextEdits").toArray();
        edits.append(edit);
        if (!forge::editor::applyEdits(target->document(), edits) && message)
            message("Completion contained invalid or overlapping edits; no text was changed.");
    };
    m_completer->popup()->hide();
    if (m_client.capabilities()
            .value("completionProvider")
            .toObject()
            .value("resolveProvider")
            .toBool())
        m_client.request("completionItem/resolve", item,
                         [item, apply](const QJsonValue& result, const QJsonObject& error) {
                             apply(error.isEmpty() && result.isObject() ? result.toObject() : item);
                         });
    else
        apply(item);
}
void PhpEditorController::definition(QPlainTextEdit* editor) {
    if (!editor || !isReady()) {
        if (message) message("Configure PHP Intelligence first.");
        return;
    }
    const QPointer<QPlainTextEdit> target(editor);
    const int revision = editor->document()->revision();
    const int caret = editor->textCursor().position();
    m_client.request(
        "textDocument/definition", parameters(editor),
        [this, target, revision, caret](const QJsonValue& result, const QJsonObject& error) {
            if (!target || target->document()->revision() != revision ||
                target->textCursor().position() != caret)
                return;
            if (!error.isEmpty()) {
                if (message) message(error.value("message").toString());
                return;
            }
            navigate(result);
        });
}
void PhpEditorController::navigate(const QJsonValue& value) {
    const auto locations = value.isArray() ? value.toArray() : QJsonArray{value};
    QStringList labels;
    QList<QJsonObject> valid;
    for (const auto& entry : locations) {
        const auto location = entry.toObject();
        const auto uri = location.value("targetUri").toString(location.value("uri").toString());
        if (QUrl(uri).toLocalFile().isEmpty()) continue;
        const auto range =
            location.value("targetSelectionRange").toObject(location.value("range").toObject());
        valid.append({{"uri", uri}, {"range", range}});
        labels.append(QUrl(uri).toLocalFile() + ':' +
                      QString::number(range.value("start").toObject().value("line").toInt() + 1));
    }
    if (valid.isEmpty()) {
        if (message) message("No definition found.");
        return;
    }
    int selected = 0;
    if (valid.size() > 1) {
        bool ok = false;
        const auto choice = QInputDialog::getItem(m_window, "Go to Definition", "Definitions",
                                                  labels, 0, false, &ok);
        if (!ok) return;
        selected = static_cast<int>(labels.indexOf(choice));
    }
    const auto location = valid.at(selected);
    const auto start = location.value("range").toObject().value("start").toObject();
    if (openLocation)
        openLocation(QUrl(location.value("uri").toString()).toLocalFile(),
                     start.value("line").toInt(), start.value("character").toInt());
}
void PhpEditorController::symbols() {
    if (!isReady()) {
        if (message) message("Configure PHP Intelligence first.");
        return;
    }
    auto* dialog = new QDialog(m_window);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Find Class or Symbol");
    dialog->resize(700, 440);
    auto* layout = new QVBoxLayout(dialog);
    auto* search = new QLineEdit(dialog);
    search->setPlaceholderText("Type a class, function, method or constant…");
    auto* list = new QListWidget(dialog);
    layout->addWidget(search);
    layout->addWidget(list);
    auto* timer = new QTimer(dialog);
    timer->setSingleShot(true);
    timer->setInterval(150);
    connect(search, &QLineEdit::textChanged, timer, qOverload<>(&QTimer::start));
    connect(timer, &QTimer::timeout, this, [this, search, list, guard = QPointer<QDialog>(dialog)] {
        const auto query = search->text();
        if (query.isEmpty()) {
            list->clear();
            return;
        }
        m_client.request(
            "workspace/symbol", {{"query", query}},
            [this, guard, search, list, query](const QJsonValue& result, const QJsonObject& error) {
                if (!guard || search->text() != query) return;
                list->clear();
                if (!error.isEmpty()) {
                    if (message) message(error.value("message").toString());
                    return;
                }
                for (const auto& entry : result.toArray()) {
                    const auto symbol = entry.toObject();
                    auto* row = new QListWidgetItem(symbol.value("name").toString() + "  " +
                                                        symbol.value("containerName").toString(),
                                                    list);
                    row->setData(Qt::UserRole, symbol.value("location"));
                    if (list->count() >= 300) break;
                }
                if (list->count()) list->setCurrentRow(0);
            });
    });
    connect(list, &QListWidget::itemActivated, dialog, [this, dialog](QListWidgetItem* item) {
        navigate(item->data(Qt::UserRole).toJsonValue());
        dialog->close();
    });
    connect(search, &QLineEdit::returnPressed, dialog, [list] {
        if (auto* item = list->currentItem()) emit list->itemActivated(item);
    });
    dialog->show();
    search->setFocus();
}
void PhpEditorController::format(QPlainTextEdit* editor) {
    if (!editor || !isReady()) return;
    const QPointer<QPlainTextEdit> target(editor);
    const int revision = editor->document()->revision();
    auto params = parameters(editor);
    params.remove("position");
    params.insert("options", QJsonObject{{"tabSize", 4}, {"insertSpaces", true}});
    m_client.request(
        "textDocument/formatting", params,
        [this, target, revision](const QJsonValue& result, const QJsonObject& error) {
            if (!target || target->document()->revision() != revision) return;
            if (!error.isEmpty()) {
                if (message) message(error.value("message").toString());
                return;
            }
            if (!forge::editor::applyEdits(target->document(), result.toArray()) && message)
                message("Invalid formatting edits; no text was changed.");
        });
}
void PhpEditorController::diagnostics(const QJsonObject& params) {
    const auto path = QUrl(params.value("uri").toString()).toLocalFile();
    for (const auto& data : std::as_const(m_documents)) {
        if (data.path != path || !data.document || data.revision != data.document->revision())
            continue;
        if (params.contains("version") && params.value("version").toInt(-1) != data.version) return;
        QVector<forge::core::Diagnostic> diagnostics;
        for (const auto& value : params.value("diagnostics").toArray()) {
            const auto item = value.toObject();
            const auto range = item.value("range").toObject();
            const auto start = range.value("start").toObject();
            const auto end = range.value("end").toObject();
            forge::core::Diagnostic diagnostic;
            diagnostic.file = path;
            diagnostic.line = start.value("line").toInt() + 1;
            diagnostic.col = start.value("character").toInt() + 1;
            diagnostic.endLine = end.value("line").toInt() + 1;
            diagnostic.endCol = end.value("character").toInt() + 1;
            diagnostic.message = item.value("message").toString();
            diagnostic.source = "PHP language server";
            diagnostic.code = item.value("code").isString()
                                  ? item.value("code").toString()
                                  : QString::number(item.value("code").toInt());
            diagnostic.severity = item.value("severity").toInt(1) == 1
                                      ? forge::core::DiagnosticSeverity::Error
                                      : forge::core::DiagnosticSeverity::Warning;
            diagnostics.append(diagnostic);
        }
        m_diagnostics->setFileDiagnostics(path, "PHP language server", diagnostics);
        return;
    }
}
bool PhpEditorController::eventFilter(QObject* object, QEvent* event) {
    auto* editor = qobject_cast<QPlainTextEdit*>(object);
    if (!editor) editor = qobject_cast<QPlainTextEdit*>(object->parent());
    if (!editor || !editor->property("forgeSourceEditor").toBool() ||
        !phpFile(editor->property("filePath").toString()))
        return false;
    if (event->type() == QEvent::MouseButtonRelease) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (object == editor->viewport() && mouse->button() == Qt::LeftButton &&
            mouse->modifiers() == Qt::ControlModifier) {
            editor->setTextCursor(editor->cursorForPosition(mouse->position().toPoint()));
            definition(editor);
            return true;
        }
    }
    if (event->type() != QEvent::KeyPress || object != editor) return false;
    const auto* key = static_cast<QKeyEvent*>(event);
    if (m_completer->popup()->isVisible() && m_completionEditor == editor &&
        (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter ||
         key->key() == Qt::Key_Tab)) {
        acceptCompletion(m_completer->popup()->currentIndex());
        return true;
    }
    if (key->key() == Qt::Key_Space && key->modifiers() == Qt::ControlModifier) {
        complete(editor);
        return true;
    }
    if (key->modifiers() == Qt::NoModifier && !key->text().isEmpty() &&
        (key->text().back().isLetterOrNumber() || QString("_>$:\\").contains(key->text().back()))) {
        QPointer<QPlainTextEdit> target(editor);
        QTimer::singleShot(
            120, this, [this, target, revision = editor->document()->revision() + 1] {
                if (target && target->document()->revision() == revision && target->hasFocus())
                    complete(target);
            });
    }
    return false;
}
void PhpEditorController::configure() {
    auto* settings = forge::core::Settings::instance();
    QDialog dialog(m_window);
    dialog.setWindowTitle("PHP Intelligence");
    auto* layout = new QFormLayout(&dialog);
    auto* node = new QLineEdit(
        settings->value("php.language.node", QStandardPaths::findExecutable("node")).toString(),
        &dialog);
    auto* script =
        new QLineEdit(settings->value("php.language.script", defaultScript()).toString(), &dialog);
    auto* version =
        new QLineEdit(settings->value("php.language.version", "8.3.0").toString(), &dialog);
    layout->addRow("Node.js 20+ executable", node);
    layout->addRow("Intelephense server .js", script);
    layout->addRow("Target PHP version", version);
    auto* description = new QLabel(
        "Local PHP indexing, completion, definitions, diagnostics and formatting.\nInstall "
        "downloads Intelephense 1.18.5 from npm into your application data.\nThe server has its "
        "own proprietary licence; it is not bundled with Forge IDE.\nNo global npm or project "
        "Composer dependencies are changed.",
        &dialog);
    description->setWordWrap(true);
    layout->addRow(description);
    auto* install = new QPushButton("Install local PHP server", &dialog);
    layout->addRow(install);
    connect(install, &QPushButton::clicked, &dialog,
            [this, &dialog, node, script] { installServer(&dialog, node->text(), script); });
    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        settings->setValue("php.language.node", node->text().trimmed());
        settings->setValue("php.language.script", script->text().trimmed());
        settings->setValue("php.language.version", version->text().trimmed());
        settings->save();
        setWorkspace(m_workspace);
    }
}
void PhpEditorController::installServer(QDialog* dialog, const QString& node, QLineEdit* script) {
    const auto npm = QStandardPaths::findExecutable("npm");
    const auto nodeDirectory = QFileInfo(node).absolutePath();
    QStringList candidates{nodeDirectory + "/node_modules/npm/bin/npm-cli.js",
                           QFileInfo(npm).canonicalPath() + "/npm-cli.js",
                           nodeDirectory + "/../lib/node_modules/npm/bin/npm-cli.js"};
    QString entry;
    for (const auto& candidate : candidates)
        if (QFileInfo::exists(candidate)) {
            entry = candidate;
            break;
        }
    if (entry.isEmpty() || node.isEmpty()) {
        QMessageBox::warning(
            dialog, "PHP Intelligence",
            "Install Node.js 20+ with npm, or configure an existing server script.");
        return;
    }
    auto* process = new QProcess(dialog);
    auto* status = new QLabel("Installing…", dialog);
    qobject_cast<QFormLayout*>(dialog->layout())->addRow(status);
    for (auto* button : dialog->findChildren<QPushButton*>()) button->setEnabled(false);
    auto finish = [dialog, status](const QString& text) {
        status->setText(text);
        status->setWordWrap(true);
        for (auto* button : dialog->findChildren<QPushButton*>()) button->setEnabled(true);
    };
    connect(process, &QProcess::errorOccurred, dialog,
            [process, finish](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) finish(process->errorString());
            });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), dialog,
            [process, script, finish](int code, QProcess::ExitStatus state) {
                if (!code && state == QProcess::NormalExit && QFileInfo::exists(defaultScript())) {
                    script->setText(defaultScript());
                    finish("Installed. Save to start indexing.");
                } else
                    finish("Installation failed: " +
                           QString::fromUtf8(process->readAllStandardError()).right(2000));
                process->deleteLater();
            });
    const auto prefix = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                        "/language-servers";
    process->start(node, {entry, "install", "--prefix", prefix, "--ignore-scripts", "--save-exact",
                          "--no-audit", "--no-fund", "intelephense@1.18.5"});
}

void PhpEditorController::scanWorkspace() {
    if (m_scanning || m_workspace.isEmpty() || !isReady()) return;
    m_scanning = true;
    const auto root = m_workspace;
    auto* watcher = new QFutureWatcher<QHash<QString, qint64>>(this);
    connect(watcher, &QFutureWatcher<QHash<QString, qint64>>::finished, this,
            [this, watcher, root] {
                m_scanning = false;
                const auto files = watcher->result();
                watcher->deleteLater();
                if (root != m_workspace) return;
                QStringList created, changed, removed;
                for (auto it = files.cbegin(); it != files.cend(); ++it) {
                    if (!m_diskFiles.contains(it.key()))
                        created.append(it.key());
                    else if (m_diskFiles.value(it.key()) != it.value())
                        changed.append(it.key());
                }
                for (auto it = m_diskFiles.cbegin(); it != m_diskFiles.cend(); ++it)
                    if (!files.contains(it.key())) removed.append(it.key());
                // Initial indexing already covers the snapshot; subsequent scans report filesystem
                // edits.
                {
                    m_client.filesChanged(created, 1);
                    m_client.filesChanged(changed, 2);
                    m_client.filesChanged(removed, 3);
                }
                m_diskFiles = files;
            });
    watcher->setFuture(QtConcurrent::run([root] {
        QHash<QString, qint64> files;
        QStringList pending{root};
        const QSet<QString> excluded{".git",   "vendor", "node_modules", "build",
                                     "target", ".forge", ".idea"};
        int visited = 0;
        while (!pending.isEmpty() && ++visited < 10000 && files.size() < 50000) {
            const auto directory = pending.takeLast();
            for (const auto& entry : QDir(directory).entryInfoList(
                     QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
                if (entry.isDir()) {
                    if (!excluded.contains(entry.fileName()))
                        pending.append(entry.absoluteFilePath());
                } else if (phpFile(entry.fileName()) || entry.fileName() == "composer.json" ||
                           entry.fileName() == "composer.lock")
                    files.insert(entry.absoluteFilePath(),
                                 entry.lastModified().toMSecsSinceEpoch() ^ entry.size());
            }
        }
        for (const auto& entry : QDir(root + "/vendor/composer")
                                     .entryInfoList({"autoload_*.php", "installed.*"}, QDir::Files))
            files.insert(entry.absoluteFilePath(),
                         entry.lastModified().toMSecsSinceEpoch() ^ entry.size());
        return files;
    }));
}
