#include "TerminalView.h"

#include <algorithm>
#include <QAbstractItemView>
#include <QApplication>
#include <QCompleter>
#include <QContextMenuEvent>
#include <QDir>
#include <QDirIterator>
#include <QFutureWatcher>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStandardPaths>
#include <QStringListModel>
#include <QtConcurrent/QtConcurrentRun>

namespace {
#ifdef Q_OS_WIN
constexpr auto matchCase = Qt::CaseInsensitive;
#else
constexpr auto matchCase = Qt::CaseSensitive;
#endif

QString quotePath(QString path) {
    if (!path.contains(QRegularExpression(QStringLiteral("[^\\w./\\\\:-]")))) return path;
#ifdef Q_OS_WIN
    path.replace('\\', '/');
    path.replace("'", "''");
#else
    path.replace("'", "'\"'\"'");
#endif
    return '\'' + path + '\'';
}

QStringList pathCandidates(const QString& input, const QString& workingDirectory) {
    if (input.contains('\n')) return {};
    int start = 0;
    QChar quote;
    for (int index = 0; index < input.size(); ++index) {
        const auto character = input.at(index);
        if (quote.isNull() && (character == '\'' || character == '"'))
            quote = character;
        else if (character == quote)
            quote = QChar();
        else if (quote.isNull() && character.isSpace())
            start = index + 1;
    }
    QString token = input.mid(start);
    if (token.startsWith('\'') || token.startsWith('"')) {
        const auto delimiter = token.front();
        token.remove(0, 1);
        if (token.endsWith(delimiter)) token.chop(1);
    }
    const auto words = input.left(start).simplified().split(' ', Qt::SkipEmptyParts);
    const bool directoryOnly =
        !words.isEmpty() && (words.first() == "cd" || words.first() == "Set-Location");
    const bool pathContext =
        directoryOnly || token.contains('/') || token.contains('\\') || token.startsWith('.') ||
        (!words.isEmpty() &&
         QStringList{"ls", "dir", "Get-ChildItem", "cat", "type", "php"}.contains(words.first())) ||
        (words.size() >= 2 && words.first() == "git" &&
         QStringList{"add", "diff", "restore", "rm", "checkout"}.contains(words.at(1)));
    if (!pathContext) return {};
    token = QDir::fromNativeSeparators(token);
    const auto slash = token.lastIndexOf('/');
    const QString directoryPart = slash >= 0 ? token.left(slash + 1) : QString();
    const QString fragment = slash >= 0 ? token.mid(slash + 1) : token;
    auto flags = QDir::NoDotAndDotDot | (directoryOnly ? QDir::Dirs : QDir::AllEntries);
    if (fragment.startsWith('.')) flags |= QDir::Hidden;
    QDirIterator entries(QDir(workingDirectory).absoluteFilePath(directoryPart), {fragment + '*'},
                         flags);
    QStringList result;
    while (entries.hasNext() && result.size() < 50) {
        entries.next();
        const auto entry = entries.fileInfo();
        if (!entry.fileName().startsWith(fragment, matchCase)) continue;
        QString path = directoryPart + entry.fileName();
        if (entry.isDir()) path += '/';
        result.append(input.left(start) + quotePath(path));
    }
    result.sort(matchCase);
    return result;
}
}  // namespace

TerminalOutputView::TerminalOutputView(QWidget* parent)
    : QPlainTextEdit(parent),
      m_inputStart(document()),
      m_workingDirectory(QDir::currentPath()),
      m_completer(new QCompleter(this)),
      m_completionModel(new QStringListModel(this)) {
    setUndoRedoEnabled(false);
    setLayoutDirection(Qt::LeftToRight);
    m_inputStart.setKeepPositionOnInsert(true);
    m_completer->setObjectName("TerminalCompleter");
    m_completer->setModel(m_completionModel);
    m_completer->setWidget(this);
    m_completer->setCaseSensitivity(matchCase);
    m_completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    m_completer->popup()->setObjectName("TerminalSuggestions");
    connect(m_completer, qOverload<const QString&>(&QCompleter::activated), this,
            [this](const QString& command) { replaceInput(command); });
    connect(this, &QPlainTextEdit::textChanged, this, &TerminalOutputView::updateInput);
    connect(this, &QPlainTextEdit::cursorPositionChanged, viewport(),
            qOverload<>(&QWidget::update));
    const QList<QPair<QString, QStringList>> commands{
        {"git",
         {"git status", "git add ", "git commit -m \"\"", "git diff", "git switch ",
          "git checkout ", "git pull", "git push", "git log --oneline"}},
        {"php",
         {"php artisan serve", "php artisan migrate", "php artisan test", "php artisan route:list",
          "php -l "}},
        {"composer", {"composer install", "composer update", "composer dump-autoload"}},
        {"npm", {"npm install", "npm run dev", "npm run build", "npm test"}},
        {"docker", {"docker compose up -d", "docker compose down", "docker ps"}}};
    for (const auto& command : commands)
        if (!QStandardPaths::findExecutable(command.first).isEmpty()) m_commands += command.second;
#ifdef Q_OS_WIN
    m_commands += QStringList{"cd ", "Get-ChildItem", "Get-Location", "Clear-Host"};
#else
    m_commands += QStringList{"cd ", "ls", "pwd", "clear"};
#endif
}

void TerminalOutputView::setCommandHandler(std::function<void(const QString&)> handler) {
    m_commandHandler = std::move(handler);
}

TerminalOutputView::~TerminalOutputView() {
    disconnect(this, nullptr, this, nullptr);
    for (auto* child : findChildren<QObject*>()) child->disconnect(this);
    m_completer->popup()->hide();
    m_completer->setWidget(nullptr);
}
void TerminalOutputView::setSharedHistory(const std::shared_ptr<QStringList>& history) {
    if (history) m_history = history;
    updateInput();
}
void TerminalOutputView::setWorkingDirectory(const QString& directory) {
    m_workingDirectory = QDir::cleanPath(QFileInfo(directory).absoluteFilePath());
    ++m_completionGeneration;
}
QString TerminalOutputView::currentInput() const {
    QTextCursor input(document());
    input.setPosition(m_inputStart.position());
    input.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    auto text = input.selectedText();
    text.replace(QChar::ParagraphSeparator, '\n');
    return text;
}
QStringList TerminalOutputView::baseCandidates(const QString& input) const {
    QStringList result;
    auto add = [&result, &input](const QString& value) {
        if (!value.contains('\n') && value != input && value.startsWith(input, matchCase) &&
            !result.contains(value) && result.size() < 50)
            result.append(value);
    };
    for (auto item = m_history->crbegin(); item != m_history->crend(); ++item) add(*item);
    for (const auto& command : m_commands) add(command);
    return result;
}
QString TerminalOutputView::suggestedSuffix() const {
    if (textCursor().hasSelection() || textCursor().position() != document()->characterCount() - 1)
        return {};
    const auto input = currentInput();
    if (input.isEmpty() || input.contains('\n')) return {};
    const auto candidates = baseCandidates(input);
    return candidates.isEmpty() ? QString() : candidates.first().mid(input.size());
}
void TerminalOutputView::updateInput() {
    const auto input = currentInput();
    if (input != m_lastInput) {
        m_lastInput = input;
        ++m_completionGeneration;
        m_completer->popup()->hide();
    }
    viewport()->update();
}
void TerminalOutputView::paintEvent(QPaintEvent* event) {
    QPlainTextEdit::paintEvent(event);
    const auto suffix = suggestedSuffix();
    if (suffix.isEmpty()) return;
    QPainter painter(viewport());
    auto color = palette().color(QPalette::Text);
    color.setAlpha(110);
    painter.setPen(color);
    painter.setFont(font());
    const auto caret = cursorRect();
    painter.drawText(caret.left(), caret.top() + fontMetrics().ascent(), suffix);
}
void TerminalOutputView::displayCandidates(const QStringList& candidates) {
    m_candidates = candidates.mid(0, 50);
    if (m_candidates.isEmpty()) return;
    m_completionModel->setStringList(m_candidates);
    m_completer->setCompletionPrefix({});
    auto rectangle = cursorRect();
    rectangle.setWidth(
        qMin(viewport()->width(), qMax(320, m_completer->popup()->sizeHintForColumn(0) + 24)));
    m_completer->complete(rectangle);
    m_completer->popup()->setCurrentIndex(m_completer->completionModel()->index(0, 0));
}
void TerminalOutputView::showCompletions() {
    ensureInputCursor();
    const auto input = currentInput();
    const auto directory = m_workingDirectory;
    const int generation = ++m_completionGeneration;
    const auto initial = baseCandidates(input);
    displayCandidates(initial);
    auto* watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcher<QStringList>::finished, this,
            [this, watcher, input, directory, generation, initial]() {
                const auto paths = watcher->result();
                watcher->deleteLater();
                if (generation != m_completionGeneration || input != currentInput() ||
                    directory != m_workingDirectory)
                    return;
                auto candidates = initial;
                for (const auto& path : paths)
                    if (!candidates.contains(path)) candidates.append(path);
                displayCandidates(candidates);
            });
    watcher->setFuture(
        QtConcurrent::run([input, directory]() { return pathCandidates(input, directory); }));
}
void TerminalOutputView::replaceInput(const QString& text) {
    QTextCursor cursor(document());
    cursor.setPosition(m_inputStart.position());
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    cursor.insertText(text);
    setTextCursor(cursor);
    m_completer->popup()->hide();
    ensureCursorVisible();
}
void TerminalOutputView::ensureInputCursor() {
    auto cursor = textCursor();
    if (cursor.position() < m_inputStart.position() ||
        (cursor.hasSelection() && cursor.selectionStart() < m_inputStart.position())) {
        cursor.movePosition(QTextCursor::End);
        setTextCursor(cursor);
    }
}
void TerminalOutputView::appendOutput(const QString& text) {
    if (text.isEmpty()) return;
    m_promptTail = (m_promptTail + text).right(4096);
    static const QRegularExpression prompt(
        QStringLiteral("(?:^|[\\r\\n]|>\\s*)(?:PS|sh) ([^>\\r\\n]+)> ?$"));
    const auto match = prompt.match(m_promptTail);
    if (match.hasMatch()) setWorkingDirectory(match.captured(1));
    const auto draft = currentInput();
    auto previous = textCursor();
    const bool editing = previous.position() >= m_inputStart.position() &&
                         previous.anchor() >= m_inputStart.position();
    const int position = previous.position() - m_inputStart.position();
    const int anchor = previous.anchor() - m_inputStart.position();
    const int scroll = verticalScrollBar()->value();
    const bool following = scroll >= verticalScrollBar()->maximum();
    QTextCursor writer(document());
    writer.setPosition(m_inputStart.position());
    writer.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    writer.removeSelectedText();
    writer.insertText(text);
    m_inputStart = writer;
    m_inputStart.setKeepPositionOnInsert(true);
    writer.insertText(draft);
    if (editing) {
        writer.setPosition(m_inputStart.position() +
                           qBound(0, anchor, static_cast<int>(draft.size())));
        writer.setPosition(
            m_inputStart.position() + qBound(0, position, static_cast<int>(draft.size())),
            QTextCursor::KeepAnchor);
        setTextCursor(writer);
    } else
        setTextCursor(previous);
    if (following)
        ensureCursorVisible();
    else
        verticalScrollBar()->setValue(scroll);
    updateInput();
}
QString TerminalOutputView::submitCurrentCommand() {
    const auto command = currentInput();
    if (!command.trimmed().isEmpty()) {
        m_history->append(command);
        while (m_history->size() > 500) m_history->removeFirst();
    }
    m_historyIndex = -1;
    m_historyDraft.clear();
    auto cursor = textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText("\n");
    m_inputStart = cursor;
    m_inputStart.setKeepPositionOnInsert(true);
    setTextCursor(cursor);
    m_completer->popup()->hide();
    updateInput();
    return command;
}
void TerminalOutputView::clearTerminal() {
    clear();
    m_inputStart = QTextCursor(document());
    m_inputStart.setKeepPositionOnInsert(true);
    m_historyIndex = -1;
    m_completer->popup()->hide();
}
void TerminalOutputView::navigateHistory(int direction) {
    if (m_history->isEmpty()) return;
    if (m_historyIndex == -1) {
        m_historyDraft = currentInput();
        m_historyIndex = static_cast<int>(m_history->size());
    }
    m_historyIndex = std::clamp(m_historyIndex + direction, 0, static_cast<int>(m_history->size()));
    replaceInput(m_historyIndex == m_history->size() ? m_historyDraft
                                                     : m_history->at(m_historyIndex));
}
void TerminalOutputView::insertFromMimeData(const QMimeData* source) {
    ensureInputCursor();
    QPlainTextEdit::insertFromMimeData(source);
}
void TerminalOutputView::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    auto* copy = menu.addAction("Copy", this, &QPlainTextEdit::copy);
    copy->setEnabled(textCursor().hasSelection());
    menu.addAction("Paste", this, &QPlainTextEdit::paste);
    menu.addAction("Select All", this, &QPlainTextEdit::selectAll);
    menu.addAction("Clear", this, &TerminalOutputView::clearTerminal);
    menu.exec(event->globalPos());
}
void TerminalOutputView::keyPressEvent(QKeyEvent* event) {
    if (event->matches(QKeySequence::Copy)) {
        copy();
        return;
    }
    if (event->matches(QKeySequence::Cut) &&
        textCursor().selectionStart() < m_inputStart.position()) {
        copy();
        return;
    }
    if (m_completer->popup()->isVisible() &&
        (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return ||
         event->key() == Qt::Key_Tab || event->key() == Qt::Key_Escape)) {
        if (event->key() == Qt::Key_Escape) {
            m_completer->popup()->hide();
        } else {
            const auto index = m_completer->popup()->currentIndex();
            if (index.isValid()) replaceInput(index.data().toString());
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Tab ||
        (event->key() == Qt::Key_Space && event->modifiers() == Qt::ControlModifier)) {
        showCompletions();
        event->accept();
        return;
    }
    if (event->modifiers() == Qt::NoModifier && event->key() == Qt::Key_Right &&
        !suggestedSuffix().isEmpty()) {
        replaceInput(currentInput() + suggestedSuffix());
        event->accept();
        return;
    }
    if (event->modifiers() == Qt::NoModifier &&
        (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
        navigateHistory(event->key() == Qt::Key_Up ? -1 : 1);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        const auto command = submitCurrentCommand();
        const auto handler = m_commandHandler;
        event->accept();
        emit commandSubmitted(command);
        if (handler) handler(command);
        return;
    }
    if (!event->text().isEmpty() || event->key() == Qt::Key_Backspace ||
        event->key() == Qt::Key_Delete)
        ensureInputCursor();
    if ((event->key() == Qt::Key_Backspace ||
         (event->key() == Qt::Key_Left && event->modifiers() == Qt::NoModifier)) &&
        textCursor().position() <= m_inputStart.position() && !textCursor().hasSelection()) {
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Backspace && event->modifiers() == Qt::ControlModifier &&
        !textCursor().hasSelection()) {
        auto cursor = textCursor();
        const int end = cursor.position();
        cursor.movePosition(QTextCursor::PreviousWord);
        const int begin = qMax(m_inputStart.position(), cursor.position());
        cursor.setPosition(end);
        cursor.setPosition(begin, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        setTextCursor(cursor);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Home) {
        auto cursor = textCursor();
        cursor.setPosition(m_inputStart.position());
        setTextCursor(cursor);
        event->accept();
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}
