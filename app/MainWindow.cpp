#include "MainWindow.h"

#include <array>
#include <cmath>
#include <functional>
#include <numbers>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFont>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QIcon>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPolygonF>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSaveFile>
#include <QRegularExpression>
#include <QSet>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTabBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextStream>
#include <QSyntaxHighlighter>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <QUrl>
#include <QWheelEvent>
#include <algorithm>
#include <utility>

#include "forge/core/ActionManager.h"
#include "forge/core/KeymapManager.h"
#include "forge/core/RpcClient.h"
#include "forge/core/ServiceSupervisor.h"
#include "forge/core/Settings.h"
#include "forge/core/TextFileCodec.h"

namespace {

constexpr int kWindowLayoutVersion = 2;

class LineEndingBlockData final : public QTextBlockUserData {
public:
    explicit LineEndingBlockData(forge::core::LineEnding value) : ending(value) {}
    forge::core::LineEnding ending;
};

void storeLineEndings(QTextDocument* document,
                      const QVector<forge::core::LineEnding>& lineEndings) {
    QTextBlock block = document->firstBlock();
    qsizetype index = 0;
    while (block.isValid() && block.next().isValid()) {
        if (index < lineEndings.size()) {
            block.setUserData(new LineEndingBlockData(lineEndings.at(index)));
        }
        ++index;
        block = block.next();
    }
}

QVector<forge::core::LineEnding> documentLineEndings(QTextDocument* document) {
    QVector<forge::core::LineEnding> lineEndings;
    QTextBlock block = document->firstBlock();
    while (block.isValid() && block.next().isValid()) {
        auto* data = dynamic_cast<LineEndingBlockData*>(block.userData());
        lineEndings.append(data ? data->ending : forge::core::LineEnding::Lf);
        block = block.next();
    }
    return lineEndings;
}

class TerminalOutputView final : public QPlainTextEdit {
public:
    using QPlainTextEdit::QPlainTextEdit;

    void setCommandHandler(std::function<void(const QString&)> handler) {
        m_commandHandler = std::move(handler);
    }

    void appendOutput(const QString& text) {
        if (text.isEmpty()) {
            return;
        }

        QTextCursor cursor(document());
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(text);
        m_commandStart = cursor.position();
        setTextCursor(cursor);
        ensureCursorVisible();
    }

    QString submitCurrentCommand() {
        QTextCursor cursor = textCursor();
        cursor.movePosition(QTextCursor::End);
        setTextCursor(cursor);
        const QString command = document()->toPlainText().mid(m_commandStart);
        if (!command.trimmed().isEmpty()) {
            m_commandHistory.append(command);
            if (m_commandHistory.size() > 500) {
                m_commandHistory.removeFirst();
            }
        }
        m_historyIndex = -1;
        m_historyDraft.clear();
        cursor.insertText(QStringLiteral("\n"));
        m_commandStart = cursor.position();
        setTextCursor(cursor);
        return command;
    }

    void clearTerminal() {
        clear();
        m_commandStart = 0;
        m_historyIndex = -1;
    }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event->modifiers() == Qt::NoModifier &&
            (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
            navigateHistory(event->key() == Qt::Key_Up ? -1 : 1);
            event->accept();
            return;
        }

        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            const QString command = submitCurrentCommand();
            if (m_commandHandler) {
                m_commandHandler(command);
            }
            event->accept();
            return;
        }

        QTextCursor cursor = textCursor();
        const bool editsDocument = !event->text().isEmpty() || event->key() == Qt::Key_Backspace ||
                                   event->key() == Qt::Key_Delete ||
                                   (event->modifiers() & Qt::ControlModifier);
        if (editsDocument &&
            (cursor.position() < m_commandStart ||
             (cursor.hasSelection() && cursor.selectionStart() < m_commandStart))) {
            cursor.setPosition(document()->characterCount() - 1);
            setTextCursor(cursor);
        }

        if (event->key() == Qt::Key_Backspace && textCursor().position() <= m_commandStart) {
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Home) {
            cursor = textCursor();
            cursor.setPosition(m_commandStart);
            setTextCursor(cursor);
            event->accept();
            return;
        }

        QPlainTextEdit::keyPressEvent(event);
        if (textCursor().position() < m_commandStart) {
            cursor = textCursor();
            cursor.setPosition(m_commandStart);
            setTextCursor(cursor);
        }
    }

private:
    void navigateHistory(int direction) {
        if (m_commandHistory.isEmpty()) {
            return;
        }

        QTextCursor cursor = textCursor();
        cursor.movePosition(QTextCursor::End);
        const QString currentInput = document()->toPlainText().mid(m_commandStart);
        if (m_historyIndex == -1) {
            m_historyDraft = currentInput;
            m_historyIndex = static_cast<int>(m_commandHistory.size());
        }
        const int historySize = static_cast<int>(m_commandHistory.size());
        m_historyIndex = std::clamp(m_historyIndex + direction, 0, historySize);
        const QString replacement =
            m_historyIndex == historySize ? m_historyDraft : m_commandHistory.at(m_historyIndex);
        cursor.setPosition(m_commandStart);
        cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        cursor.insertText(replacement);
        setTextCursor(cursor);
    }

    int m_commandStart = 0;
    QStringList m_commandHistory;
    QString m_historyDraft;
    int m_historyIndex = -1;
    std::function<void(const QString&)> m_commandHandler;
};

QString themedColor(const QJsonObject& colors, const QString& key, const QString& fallback);

class ForgeCodeEditor;
class ForgeSyntaxHighlighter;

class LineNumberArea final : public QWidget {
public:
    explicit LineNumberArea(ForgeCodeEditor* editor);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    ForgeCodeEditor* m_editor;
};

class ForgeCodeEditor final : public QPlainTextEdit {
public:
    explicit ForgeCodeEditor(QWidget* parent = nullptr)
        : QPlainTextEdit(parent), m_lineNumberArea(new LineNumberArea(this)) {
        connect(this, &QPlainTextEdit::blockCountChanged, this,
                [this]() { updateLineNumberAreaWidth(); });
        connect(this, &QPlainTextEdit::updateRequest, this,
                [this](const QRect& rect, int dy) { updateLineNumberArea(rect, dy); });
        connect(this, &QPlainTextEdit::cursorPositionChanged, this,
                [this]() { highlightCurrentLine(); });
        connect(document(), &QTextDocument::contentsChanged, this,
                [this]() { refreshFunctionFolds(); });
        updateLineNumberAreaWidth();
        highlightCurrentLine();
    }

    void setSyntaxHighlighter(QSyntaxHighlighter* highlighter) {
        m_syntaxHighlighter = highlighter;
    }

    QSyntaxHighlighter* syntaxHighlighter() const {
        return m_syntaxHighlighter;
    }

    void setThemeFont(QFont font) {
        m_themeFontSize = qMax(8, font.pointSize());
        font.setPointSize(qBound(8, m_themeFontSize + m_zoomOffset, 36));
        QPlainTextEdit::setFont(font);
        setTabStopDistance(QFontMetrics(font).horizontalAdvance(QLatin1Char(' ')) * 4);
    }

    int lineNumberAreaWidth() const {
        const int digits = qMax(2, QString::number(qMax(1, blockCount())).size());
        return 16 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    }

    void setEditorColors(const QColor& gutterBackground, const QColor& gutterForeground,
                         const QColor& activeLine, const QColor& gutterBorder,
                         const QColor& activeLineForeground) {
        m_gutterBackground = gutterBackground;
        m_gutterForeground = gutterForeground;
        m_activeLine = activeLine;
        m_gutterBorder = gutterBorder;
        m_activeLineForeground = activeLineForeground;
        updateLineNumberAreaWidth();
        m_lineNumberArea->update();
        highlightCurrentLine();
    }

    void toggleFoldAtY(int y) {
        QTextBlock block = firstVisibleBlock();
        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());
        while (block.isValid() && top <= y) {
            if (y < bottom && m_functionFolds.contains(block.blockNumber())) {
                const int start = block.blockNumber();
                if (m_collapsedFolds.contains(start)) {
                    m_collapsedFolds.remove(start);
                } else {
                    m_collapsedFolds.insert(start);
                }
                applyCollapsedFolds();
                return;
            }
            block = block.next();
            top = bottom;
            bottom = top + qRound(blockBoundingRect(block).height());
        }
    }

    void paintLineNumbers(QPaintEvent* event) {
        QPainter painter(m_lineNumberArea);
        painter.fillRect(event->rect(), m_gutterBackground);
        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());
        const int currentBlock = textCursor().blockNumber();

        while (block.isValid() && top <= event->rect().bottom()) {
            if (block.isVisible() && bottom >= event->rect().top()) {
                const bool hasFold = m_functionFolds.contains(blockNumber);
                if (hasFold) {
                    const bool collapsed = m_collapsedFolds.contains(blockNumber);
                    const int centerY = top + fontMetrics().height() / 2;
                    QPolygonF arrow;
                    if (collapsed) {
                        arrow << QPointF(4, centerY - 4) << QPointF(10, centerY)
                              << QPointF(4, centerY + 4);
                    } else {
                        arrow << QPointF(3, centerY - 2) << QPointF(11, centerY - 2)
                              << QPointF(7, centerY + 3);
                    }
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(m_gutterForeground);
                    painter.drawPolygon(arrow);
                }
                painter.setPen(blockNumber == currentBlock ? m_activeLineForeground
                                                           : m_gutterForeground);
                painter.drawText(13, top, m_lineNumberArea->width() - 15, fontMetrics().height(),
                                 Qt::AlignRight | Qt::AlignVCenter,
                                 QString::number(blockNumber + 1));
            }
            block = block.next();
            top = bottom;
            bottom = top + qRound(blockBoundingRect(block).height());
            ++blockNumber;
        }
        painter.setPen(m_gutterBorder);
        painter.drawLine(m_lineNumberArea->width() - 1, event->rect().top(),
                         m_lineNumberArea->width() - 1, event->rect().bottom());
    }

protected:
    void wheelEvent(QWheelEvent* event) override {
        const int wheelDelta = event->angleDelta().y();
        if (event->modifiers().testFlag(Qt::ControlModifier) && wheelDelta != 0) {
            const int currentSize = font().pointSize();
            const int newSize = qBound(8, currentSize + (wheelDelta > 0 ? 1 : -1), 36);
            m_zoomOffset += newSize - currentSize;
            QFont zoomedFont = font();
            zoomedFont.setPointSize(newSize);
            QPlainTextEdit::setFont(zoomedFont);
            setTabStopDistance(QFontMetrics(zoomedFont).horizontalAdvance(QLatin1Char(' ')) * 4);
            event->accept();
            return;
        }
        QPlainTextEdit::wheelEvent(event);
    }

    void resizeEvent(QResizeEvent* event) override {
        QPlainTextEdit::resizeEvent(event);
        const QRect contents = contentsRect();
        m_lineNumberArea->setGeometry(
            QRect(contents.left(), contents.top(), lineNumberAreaWidth(), contents.height()));
    }

private:
    void refreshFunctionFolds() {
        m_functionFolds.clear();
        m_collapsedFolds.clear();
        QTextBlock visibleBlock = document()->firstBlock();
        while (visibleBlock.isValid()) {
            visibleBlock.setVisible(true);
            visibleBlock.setLineCount(qMax(1, visibleBlock.layout()->lineCount()));
            visibleBlock = visibleBlock.next();
        }

        struct BraceFrame {
            int blockNumber;
            bool functionBody;
        };
        QVector<BraceFrame> braces;
        bool inBlockComment = false;
        bool awaitingFunctionBody = false;
        int awaitingFunctionBlockNumber = -1;
        QTextBlock block = document()->firstBlock();
        while (block.isValid()) {
            const QString line = block.text();
            QChar quote;
            bool escaped = false;
            for (int index = 0; index < line.size(); ++index) {
                const QChar current = line.at(index);
                const QChar next = index + 1 < line.size() ? line.at(index + 1) : QChar();
                if (inBlockComment) {
                    if (current == QLatin1Char('*') && next == QLatin1Char('/')) {
                        inBlockComment = false;
                        ++index;
                    }
                    continue;
                }
                if (!quote.isNull()) {
                    if (escaped) {
                        escaped = false;
                    } else if (current == QLatin1Char('\\')) {
                        escaped = true;
                    } else if (current == quote) {
                        quote = QChar();
                    }
                    continue;
                }
                if (current == QLatin1Char('/') && next == QLatin1Char('/')) {
                    break;
                }
                if (current == QLatin1Char('#') && next != QLatin1Char('[')) {
                    break;
                }
                if (current == QLatin1Char('/') && next == QLatin1Char('*')) {
                    inBlockComment = true;
                    ++index;
                    continue;
                }
                if (current == QLatin1Char('"') || current == QLatin1Char('\'')) {
                    quote = current;
                    continue;
                }
                if (current.isLetter() || current == QLatin1Char('_')) {
                    const int wordStart = index;
                    while (index + 1 < line.size() &&
                           (line.at(index + 1).isLetterOrNumber() ||
                            line.at(index + 1) == QLatin1Char('_'))) {
                        ++index;
                    }
                    if (line.mid(wordStart, index - wordStart + 1) ==
                        QStringLiteral("function")) {
                        awaitingFunctionBody = true;
                        awaitingFunctionBlockNumber = block.blockNumber();
                    }
                    continue;
                }
                if (current == QLatin1Char('{')) {
                    braces.append({awaitingFunctionBody ? awaitingFunctionBlockNumber
                                                       : block.blockNumber(),
                                   awaitingFunctionBody});
                    awaitingFunctionBody = false;
                    awaitingFunctionBlockNumber = -1;
                } else if (current == QLatin1Char('}')) {
                    if (!braces.isEmpty()) {
                        const BraceFrame frame = braces.takeLast();
                        if (frame.functionBody && block.blockNumber() > frame.blockNumber) {
                            m_functionFolds.insert(frame.blockNumber, block.blockNumber());
                        }
                    }
                } else if (current == QLatin1Char(';')) {
                    awaitingFunctionBody = false;
                    awaitingFunctionBlockNumber = -1;
                }
            }
            block = block.next();
        }
        m_lineNumberArea->update();
    }

    void applyCollapsedFolds() {
        QTextBlock block = document()->firstBlock();
        while (block.isValid()) {
            const int blockNumber = block.blockNumber();
            bool hidden = false;
            for (int start : std::as_const(m_collapsedFolds)) {
                const int end = m_functionFolds.value(start, start);
                if (blockNumber > start && blockNumber <= end) {
                    hidden = true;
                    break;
                }
            }
            block.setVisible(!hidden);
            block.setLineCount(hidden ? 0 : qMax(1, block.layout()->lineCount()));
            block = block.next();
        }
        document()->markContentsDirty(0, document()->characterCount());
        viewport()->update();
        m_lineNumberArea->update();
    }

    void updateLineNumberAreaWidth() {
        const int width = lineNumberAreaWidth();
        setViewportMargins(width, 0, 0, 0);
        const QRect contents = contentsRect();
        m_lineNumberArea->setGeometry(
            QRect(contents.left(), contents.top(), width, contents.height()));
    }

    void updateLineNumberArea(const QRect& rect, int dy) {
        if (dy != 0) {
            m_lineNumberArea->scroll(0, dy);
        } else {
            m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
        }
        if (rect.contains(viewport()->rect())) {
            updateLineNumberAreaWidth();
        }
    }

    void highlightCurrentLine() {
        QList<QTextEdit::ExtraSelection> selections;
        if (!isReadOnly()) {
            QTextEdit::ExtraSelection selection;
            selection.format.setBackground(m_activeLine);
            selection.format.setProperty(QTextFormat::FullWidthSelection, true);
            selection.cursor = textCursor();
            selection.cursor.clearSelection();
            selections.append(selection);
        }
        setExtraSelections(selections);
        m_lineNumberArea->update();
    }

    LineNumberArea* m_lineNumberArea;
    QSyntaxHighlighter* m_syntaxHighlighter = nullptr;
    QHash<int, int> m_functionFolds;
    QSet<int> m_collapsedFolds;
    int m_themeFontSize = 14;
    int m_zoomOffset = 0;
    QColor m_gutterBackground = QColor(QStringLiteral("#1E1F22"));
    QColor m_gutterForeground = QColor(QStringLiteral("#636573"));
    QColor m_activeLine = QColor(QStringLiteral("#26282E"));
    QColor m_gutterBorder = QColor(QStringLiteral("#2B2D30"));
    QColor m_activeLineForeground = QColor(QStringLiteral("#A8B2C1"));
};

LineNumberArea::LineNumberArea(ForgeCodeEditor* editor) : QWidget(editor), m_editor(editor) {}

QSize LineNumberArea::sizeHint() const {
    return QSize(m_editor->lineNumberAreaWidth(), 0);
}

void LineNumberArea::paintEvent(QPaintEvent* event) {
    m_editor->paintLineNumbers(event);
}

void LineNumberArea::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_editor->toggleFoldAtY(qRound(event->position().y()));
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

class ForgeSyntaxHighlighter final : public QSyntaxHighlighter {
public:
    ForgeSyntaxHighlighter(QTextDocument* document, QString language, QObject* parent)
        : QSyntaxHighlighter(document), m_language(std::move(language)) {
        setParent(parent);
    }

    void setTheme(const QJsonObject& syntax) {
        m_keyword = formatFor(syntax, QStringLiteral("keyword"), QStringLiteral("#CF8EF4"));
        m_type = formatFor(syntax, QStringLiteral("type"), QStringLiteral("#E5C07B"));
        m_string = formatFor(syntax, QStringLiteral("string"), QStringLiteral("#98C379"));
        m_comment = formatFor(syntax, QStringLiteral("comment"), QStringLiteral("#7F8490"));
        m_number = formatFor(syntax, QStringLiteral("number"), QStringLiteral("#D19A66"));
        m_variable = formatFor(syntax, QStringLiteral("phpVariable"), QStringLiteral("#E06C75"));
        m_tag = formatFor(syntax, QStringLiteral("tag"), QStringLiteral("#E06C75"));
        m_attribute = formatFor(syntax, QStringLiteral("tagAttribute"), QStringLiteral("#D19A66"));
        m_blade = formatFor(syntax, QStringLiteral("bladeDirective"), QStringLiteral("#CF8EF4"));
        m_function = formatFor(syntax, QStringLiteral("function"), QStringLiteral("#61AFEF"));
        rehighlight();
    }

protected:
    void highlightBlock(const QString& text) override {
        const QString language = m_language.toLower();
        const bool php = language == QStringLiteral("php") || language == QStringLiteral("blade");
        const bool html = language == QStringLiteral("html") || language == QStringLiteral("htm") ||
                          language == QStringLiteral("vue") ||
                          language == QStringLiteral("blade") || language == QStringLiteral("xml");
        const bool script = php || language == QStringLiteral("js") ||
                            language == QStringLiteral("jsx") || language == QStringLiteral("ts") ||
                            language == QStringLiteral("tsx") || language == QStringLiteral("py") ||
                            language == QStringLiteral("sql") ||
                            language == QStringLiteral("json") ||
                            language == QStringLiteral("yaml") || language == QStringLiteral("yml");
        const bool stylesheet =
            language == QStringLiteral("css") || language == QStringLiteral("scss");

        if (script || stylesheet) {
            const QString keywords =
                php ? QStringLiteral(
                          "\\b(class|interface|trait|enum|function|public|protected|private|"
                          "static|abstract|final|const|namespace|use|new|return|if|else|"
                          "elseif|foreach|for|while|switch|case|break|continue|try|catch|"
                          "throw|extends|implements|this|self|parent|as|echo|yield|match|"
                          "fn|async|await)\\b")
                    : QStringLiteral(
                          "\\b(class|function|def|async|await|return|if|else|elif|for|"
                          "while|switch|case|break|continue|try|catch|throw|new|const|"
                          "let|var|import|from|export|extends|implements|public|private|"
                          "static|true|false|null|None|SELECT|FROM|WHERE|INSERT|UPDATE|"
                          "DELETE|CREATE|TABLE|JOIN|ON|AS|AND|OR|NOT)\\b");
            applyPattern(text,
                         QRegularExpression(keywords, QRegularExpression::CaseInsensitiveOption),
                         m_keyword);
            applyPattern(text, QRegularExpression(QStringLiteral("\\b\\d+(?:\\.\\d+)?\\b")),
                         m_number);
            applyPattern(text, QRegularExpression(QStringLiteral("\\b[A-Z][A-Za-z0-9_]*\\b")),
                         m_type);
            applyPattern(text,
                         QRegularExpression(QStringLiteral("\\b[A-Za-z_][A-Za-z0-9_]*"
                                                           "(?=\\s*\\()")),
                         m_function);
            if (php) {
                applyPattern(text, QRegularExpression(QStringLiteral("\\$[A-Za-z_][A-Za-z0-9_]*")),
                             m_variable);
            }
        }
        if (stylesheet) {
            applyPattern(text, QRegularExpression(QStringLiteral("[^{}]+(?=\\s*\\{)")), m_type);
            applyPattern(text,
                         QRegularExpression(QStringLiteral("[-A-Za-z_][A-Za-z0-9_-]*"
                                                           "(?=\\s*:)")),
                         m_attribute);
            applyPattern(text, QRegularExpression(QStringLiteral("#[0-9A-Fa-f]{3,8}\\b")),
                         m_number);
        }

        if (html) {
            applyPattern(text, QRegularExpression(QStringLiteral("</?[A-Za-z][^>]*>")), m_tag);
            applyPattern(text,
                         QRegularExpression(QStringLiteral("\\b[A-Za-z_:][-A-Za-z0-9_:]*"
                                                           "(?=\\s*=)")),
                         m_attribute);
        }
        if (language == QStringLiteral("blade") || php) {
            applyPattern(text, QRegularExpression(QStringLiteral("@[A-Za-z_][A-Za-z0-9_]*")),
                         m_blade);
        }

        applyPattern(
            text,
            QRegularExpression(QStringLiteral("\"(?:\\\\.|[^\"\\\\])*\"|'(?:\\\\.|[^'\\\\])*'")),
            m_string);
        applyPattern(text, QRegularExpression(QStringLiteral("//[^\\n]*|#[^\\n]*")), m_comment);
        highlightBlockComment(text);
    }

private:
    static QTextCharFormat formatFor(const QJsonObject& syntax, const QString& key,
                                     const QString& fallback) {
        QTextCharFormat format;
        format.setForeground(QColor(themedColor(syntax, key, fallback)));
        return format;
    }

    void applyPattern(const QString& text, const QRegularExpression& expression,
                      const QTextCharFormat& format) {
        auto match = expression.globalMatch(text);
        while (match.hasNext()) {
            const auto item = match.next();
            setFormat(item.capturedStart(), item.capturedLength(), format);
        }
    }

    void highlightBlockComment(const QString& text) {
        int start = previousBlockState() == 1 ? 0 : text.indexOf(QStringLiteral("/*"));
        while (start >= 0) {
            const int end = text.indexOf(QStringLiteral("*/"), start);
            if (end < 0) {
                setCurrentBlockState(1);
                setFormat(start, text.size() - start, m_comment);
                return;
            }
            const int length = end - start + 2;
            setFormat(start, length, m_comment);
            start = text.indexOf(QStringLiteral("/*"), end + 2);
        }
        setCurrentBlockState(0);
    }

    QString m_language;
    QTextCharFormat m_keyword;
    QTextCharFormat m_type;
    QTextCharFormat m_string;
    QTextCharFormat m_comment;
    QTextCharFormat m_number;
    QTextCharFormat m_variable;
    QTextCharFormat m_tag;
    QTextCharFormat m_attribute;
    QTextCharFormat m_blade;
    QTextCharFormat m_function;
};

QString themeResourcePath(const QString& name) {
    return name.compare(QStringLiteral("Daylight"), Qt::CaseInsensitive) == 0
               ? QStringLiteral(":/themes/daylight.json")
               : QStringLiteral(":/themes/graphite.json");
}

QString themedColor(const QJsonObject& colors, const QString& key, const QString& fallback) {
    const QString color = colors.value(key).toString(fallback);
    return QColor(color).isValid() ? color : fallback;
}

QString fileLanguageName(const QFileInfo& info) {
    const QString name = info.fileName().toLower();
    if (name.endsWith(QStringLiteral(".blade.php"))) return QStringLiteral("Blade template");
    if (name.endsWith(QStringLiteral(".php"))) return QStringLiteral("PHP");
    if (name.endsWith(QStringLiteral(".tsx"))) return QStringLiteral("TypeScript React");
    if (name.endsWith(QStringLiteral(".ts"))) return QStringLiteral("TypeScript");
    if (name.endsWith(QStringLiteral(".jsx"))) return QStringLiteral("JavaScript React");
    if (name.endsWith(QStringLiteral(".js"))) return QStringLiteral("JavaScript");
    if (name.endsWith(QStringLiteral(".vue"))) return QStringLiteral("Vue");
    if (name.endsWith(QStringLiteral(".json"))) return QStringLiteral("JSON");
    if (name.endsWith(QStringLiteral(".yaml")) || name.endsWith(QStringLiteral(".yml"))) {
        return QStringLiteral("YAML");
    }
    if (name.endsWith(QStringLiteral(".xml"))) return QStringLiteral("XML");
    if (name.endsWith(QStringLiteral(".md"))) return QStringLiteral("Markdown");
    if (name.endsWith(QStringLiteral(".py"))) return QStringLiteral("Python");
    if (name.endsWith(QStringLiteral(".sql"))) return QStringLiteral("SQL");
    if (name.endsWith(QStringLiteral(".html")) || name.endsWith(QStringLiteral(".htm"))) {
        return QStringLiteral("HTML");
    }
    if (name.endsWith(QStringLiteral(".scss"))) return QStringLiteral("SCSS");
    if (name.endsWith(QStringLiteral(".css"))) return QStringLiteral("CSS");
    if (name.endsWith(QStringLiteral(".env")) || name.startsWith(QStringLiteral(".env."))) {
        return QStringLiteral("Environment");
    }
    if (name == QStringLiteral("dockerfile") || name.startsWith(QStringLiteral("dockerfile."))) {
        return QStringLiteral("Dockerfile");
    }
    if (name == QStringLiteral("makefile")) return QStringLiteral("Makefile");
    return QStringLiteral("Text");
}

void collectProjectFiles(const QString& directory, QStringList& files, int depth = 0) {
    if (files.size() >= 15000 || depth >= 40) {
        return;
    }

    const QDir current(directory);
    const QFileInfoList fileEntries =
        current.entryInfoList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& file : fileEntries) {
        files.append(file.absoluteFilePath());
        if (files.size() >= 15000) {
            return;
        }
    }

    static const QStringList excludedDirectories = {
        QStringLiteral(".git"), QStringLiteral("node_modules"), QStringLiteral("vendor"),
        QStringLiteral("build"), QStringLiteral(".idea")};
    const QFileInfoList directories =
        current.entryInfoList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& child : directories) {
        if (!excludedDirectories.contains(child.fileName())) {
            collectProjectFiles(child.absoluteFilePath(), files, depth + 1);
        }
        if (files.size() >= 15000) {
            return;
        }
    }
}

QString cachegrindTopFunctions(const QString& path)
{
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
            events = line.mid(7).split(QRegularExpression(QStringLiteral("\\s+")),
                                       Qt::SkipEmptyParts);
        } else if (line.startsWith(QStringLiteral("fn="))) {
            currentFunction = line.mid(3);
        } else if (!currentFunction.isEmpty() && !line.isEmpty() &&
                   line.front().isDigit()) {
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
    std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
        return left.second > right.second;
    });

    QStringList lines{
        QStringLiteral("Xdebug profile (self cost: %1)").arg(events.constFirst()),
        QStringLiteral("Largest self-cost totals; these are profiler event counts, not wall-clock "
                       "percentages. Source: %1").arg(path)};
    const int limit = qMin(15, static_cast<int>(ranked.size()));
    for (int index = 0; index < limit; ++index) {
        lines.append(QStringLiteral("%1. %2 — %3")
                         .arg(index + 1)
                         .arg(ranked.at(index).first)
                         .arg(ranked.at(index).second));
    }
    return lines.join(QLatin1Char('\n'));
}

class ForgeFileSystemModel final : public QFileSystemModel {
public:
    using QFileSystemModel::QFileSystemModel;

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (role == Qt::ToolTipRole && index.isValid()) {
            const QFileInfo info(filePath(index));
            if (info.isDir()) {
                return info.absoluteFilePath();
            }
            return QStringLiteral("%1\nLanguage: %2\n%3")
                .arg(info.fileName(), fileLanguageName(info), info.absoluteFilePath());
        }
        return QFileSystemModel::data(index, role);
    }
};

QIcon forgeIcon(const QString& name, const QColor& foreground, const QColor& accent) {
    QPixmap pixmap(48, 48);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(2.0, 2.0);
    painter.setPen(QPen(foreground, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    if (name == QStringLiteral("folder") || name == QStringLiteral("project")) {
        QPainterPath folder;
        folder.moveTo(3.5, 7.5);
        folder.lineTo(10, 7.5);
        folder.lineTo(12.5, 10);
        folder.lineTo(20.5, 10);
        folder.quadTo(21.5, 10, 21.5, 11.5);
        folder.lineTo(21.5, 18);
        folder.quadTo(21.5, 19.5, 20, 19.5);
        folder.lineTo(4, 19.5);
        folder.quadTo(2.5, 19.5, 2.5, 18);
        folder.lineTo(2.5, 9);
        folder.quadTo(2.5, 7.5, 3.5, 7.5);
        painter.drawPath(folder);
        painter.setPen(QPen(accent, 1.4, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(4.5, 13), QPointF(19.5, 13));
    } else if (name == QStringLiteral("file") || name == QStringLiteral("fileOpen") ||
               name.startsWith(QStringLiteral("code:"))) {
        const bool codeFile = name.startsWith(QStringLiteral("code:"));
        QPainterPath file;
        file.moveTo(6, 2.5);
        file.lineTo(14, 2.5);
        file.lineTo(19, 7.5);
        file.lineTo(19, 20.5);
        file.quadTo(19, 21.5, 18, 21.5);
        file.lineTo(6, 21.5);
        file.quadTo(5, 21.5, 5, 20.5);
        file.lineTo(5, 3.5);
        file.quadTo(5, 2.5, 6, 2.5);
        file.moveTo(14, 3);
        file.lineTo(14, 7);
        file.quadTo(14, 8, 15, 8);
        file.lineTo(18.5, 8);
        painter.drawPath(file);
        painter.setPen(QPen(accent, 1.5, Qt::SolidLine, Qt::RoundCap));
        if (codeFile) {
            painter.setFont(QFont(QStringLiteral("Segoe UI"), 5, QFont::Bold));
            painter.drawText(QRectF(5, 12, 14, 8), Qt::AlignCenter, name.mid(5));
        } else {
            painter.drawLine(QPointF(8, 12), QPointF(16, 12));
            painter.drawLine(QPointF(8, 15.5), QPointF(16, 15.5));
            painter.drawLine(QPointF(8, 19), QPointF(13, 19));
        }
    } else if (name == QStringLiteral("save")) {
        QPainterPath disk;
        disk.moveTo(4, 3);
        disk.lineTo(17.5, 3);
        disk.lineTo(21, 6.5);
        disk.lineTo(21, 20);
        disk.quadTo(21, 21, 20, 21);
        disk.lineTo(4, 21);
        disk.quadTo(3, 21, 3, 20);
        disk.lineTo(3, 4);
        disk.quadTo(3, 3, 4, 3);
        painter.drawPath(disk);
        painter.drawRect(QRectF(7, 3.5, 9, 6));
        painter.drawRect(QRectF(7, 13, 10, 7.5));
    } else if (name == QStringLiteral("settings")) {
        QPolygonF gear;
        for (int point = 0; point < 24; ++point) {
            const qreal angle = point * std::numbers::pi / 12.0;
            const qreal radius = point % 3 == 0 ? 10.5 : 8.5;
            gear << QPointF(12 + radius * std::cos(angle), 12 + radius * std::sin(angle));
        }
        painter.drawPolygon(gear);
        painter.drawEllipse(QPointF(12, 12), 3.2, 3.2);
    } else if (name == QStringLiteral("terminal")) {
        painter.drawRoundedRect(QRectF(2.5, 4, 19, 16), 2, 2);
        QPainterPath prompt;
        prompt.moveTo(6, 9);
        prompt.lineTo(9, 12);
        prompt.lineTo(6, 15);
        painter.drawPath(prompt);
        painter.drawLine(QPointF(11, 15), QPointF(16, 15));
    } else if (name == QStringLiteral("problems")) {
        QPolygonF warning;
        warning << QPointF(12, 3) << QPointF(21.5, 20) << QPointF(2.5, 20);
        painter.drawPolygon(warning);
        painter.drawLine(QPointF(12, 9), QPointF(12, 14));
        painter.setBrush(foreground);
        painter.drawEllipse(QPointF(12, 17), 0.8, 0.8);
    } else if (name == QStringLiteral("run")) {
        QPolygonF play;
        play << QPointF(7, 4) << QPointF(20, 12) << QPointF(7, 20);
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        painter.drawPolygon(play);
    } else if (name == QStringLiteral("structure")) {
        painter.drawLine(QPointF(6, 5), QPointF(6, 18));
        painter.drawLine(QPointF(6, 9), QPointF(12, 9));
        painter.drawLine(QPointF(6, 16), QPointF(12, 16));
        painter.setBrush(accent);
        painter.drawEllipse(QPointF(6, 4), 2, 2);
        painter.drawEllipse(QPointF(14, 9), 2, 2);
        painter.drawEllipse(QPointF(14, 16), 2, 2);
    } else if (name == QStringLiteral("stop")) {
        painter.setBrush(accent);
        painter.drawRoundedRect(QRectF(5, 5, 14, 14), 2, 2);
    } else if (name == QStringLiteral("git")) {
        QPainterPath branch;
        branch.moveTo(7, 4);
        branch.lineTo(7, 18);
        branch.cubicTo(7, 12, 17, 16, 17, 9);
        painter.drawPath(branch);
        painter.setBrush(accent);
        painter.drawEllipse(QPointF(7, 4), 2.2, 2.2);
        painter.drawEllipse(QPointF(7, 19), 2.2, 2.2);
        painter.drawEllipse(QPointF(17, 8), 2.2, 2.2);
    } else if (name == QStringLiteral("close")) {
        painter.drawLine(QPointF(7, 7), QPointF(17, 17));
        painter.drawLine(QPointF(17, 7), QPointF(7, 17));
    } else if (name == QStringLiteral("search")) {
        painter.drawEllipse(QPointF(10, 10), 6, 6);
        painter.drawLine(QPointF(14.5, 14.5), QPointF(20, 20));
    } else if (name == QStringLiteral("ai")) {
        painter.drawRoundedRect(QRectF(3, 4, 18, 14), 3, 3);
        painter.drawLine(QPointF(8, 18), QPointF(6, 21));
        painter.drawLine(QPointF(9, 9), QPointF(9, 13));
        painter.drawLine(QPointF(15, 9), QPointF(15, 13));
        painter.setBrush(accent);
        painter.drawEllipse(QPointF(9, 9), 1, 1);
        painter.drawEllipse(QPointF(15, 9), 1, 1);
    } else {
        QPainterPath mark;
        mark.moveTo(5, 19);
        mark.lineTo(5, 5);
        mark.lineTo(19, 5);
        mark.moveTo(5, 12);
        mark.lineTo(15, 12);
        painter.setPen(QPen(accent, 2.5, Qt::SolidLine, Qt::SquareCap));
        painter.drawPath(mark);
    }

    return QIcon(pixmap);
}

class ForgeFileIconProvider final : public QFileIconProvider {
public:
    ForgeFileIconProvider(QColor foreground, QColor accent)
        : m_foreground(std::move(foreground)), m_accent(std::move(accent)) {
        primeIcons();
    }

    void setColors(QColor foreground, QColor accent) {
        QMutexLocker locker(&m_mutex);
        m_foreground = std::move(foreground);
        m_accent = std::move(accent);
        m_icons.clear();
        primeIcons();
    }

    QIcon icon(const QFileInfo& info) const override {
        QMutexLocker locker(&m_mutex);
        if (info.isDir()) {
            return cachedIcon(QStringLiteral("folder"), QStringLiteral("folder"), m_accent);
        }

        const QString fileName = info.fileName().toLower();
        QString badge;
        QColor badgeColor = m_accent;
        if (fileName.endsWith(QStringLiteral(".blade.php"))) {
            badge = QStringLiteral("BLD");
            badgeColor = QColor(QStringLiteral("#A98BFF"));
        } else if (fileName.endsWith(QStringLiteral(".php"))) {
            badge = QStringLiteral("PHP");
            badgeColor = QColor(QStringLiteral("#A98BFF"));
        } else if (fileName.endsWith(QStringLiteral(".tsx")) ||
                   fileName.endsWith(QStringLiteral(".ts"))) {
            badge = QStringLiteral("TS");
            badgeColor = QColor(QStringLiteral("#62A7E8"));
        } else if (fileName.endsWith(QStringLiteral(".jsx")) ||
                   fileName.endsWith(QStringLiteral(".js"))) {
            badge = QStringLiteral("JS");
            badgeColor = QColor(QStringLiteral("#E6C64A"));
        } else if (fileName.endsWith(QStringLiteral(".json"))) {
            badge = QStringLiteral("{ }");
            badgeColor = QColor(QStringLiteral("#71B8D0"));
        } else if (fileName.endsWith(QStringLiteral(".css")) ||
                   fileName.endsWith(QStringLiteral(".scss"))) {
            badge = QStringLiteral("CSS");
            badgeColor = QColor(QStringLiteral("#D789C8"));
        } else if (fileName.endsWith(QStringLiteral(".html")) ||
                   fileName.endsWith(QStringLiteral(".vue"))) {
            badge = QStringLiteral("HTML");
            badgeColor = QColor(QStringLiteral("#E77B56"));
        } else if (fileName.endsWith(QStringLiteral(".yml")) ||
                   fileName.endsWith(QStringLiteral(".yaml"))) {
            badge = QStringLiteral("YML");
            badgeColor = QColor(QStringLiteral("#D2798B"));
        } else if (fileName.endsWith(QStringLiteral(".md"))) {
            badge = QStringLiteral("MD");
            badgeColor = QColor(QStringLiteral("#7DA6C6"));
        } else if (fileName.endsWith(QStringLiteral(".sql"))) {
            badge = QStringLiteral("SQL");
            badgeColor = QColor(QStringLiteral("#66B7A7"));
        } else if (fileName.endsWith(QStringLiteral(".py"))) {
            badge = QStringLiteral("PY");
            badgeColor = QColor(QStringLiteral("#E6C64A"));
        } else if (fileName == QStringLiteral(".env") ||
                   fileName.startsWith(QStringLiteral(".env."))) {
            badge = QStringLiteral("ENV");
            badgeColor = QColor(QStringLiteral("#83BB76"));
        } else if (fileName == QStringLiteral("dockerfile") ||
                   fileName.startsWith(QStringLiteral("dockerfile."))) {
            badge = QStringLiteral("DKR");
            badgeColor = QColor(QStringLiteral("#64A9D7"));
        }

        const QString iconName =
            badge.isEmpty() ? QStringLiteral("file") : QStringLiteral("code:") + badge;
        return cachedIcon(iconName, iconName, badgeColor);
    }

private:
    void primeIcons() const {
        // QFileSystemModel requests icons on a worker thread, so create pixmaps here first.
        cachedIcon(QStringLiteral("folder"), QStringLiteral("folder"), m_accent);
        cachedIcon(QStringLiteral("file"), QStringLiteral("file"), m_accent);

        const std::array<std::pair<QString, QColor>, 13> badges{{
            {QStringLiteral("BLD"), QColor(QStringLiteral("#A98BFF"))},
            {QStringLiteral("PHP"), QColor(QStringLiteral("#A98BFF"))},
            {QStringLiteral("TS"), QColor(QStringLiteral("#62A7E8"))},
            {QStringLiteral("JS"), QColor(QStringLiteral("#E6C64A"))},
            {QStringLiteral("{ }"), QColor(QStringLiteral("#71B8D0"))},
            {QStringLiteral("CSS"), QColor(QStringLiteral("#D789C8"))},
            {QStringLiteral("HTML"), QColor(QStringLiteral("#E77B56"))},
            {QStringLiteral("YML"), QColor(QStringLiteral("#D2798B"))},
            {QStringLiteral("MD"), QColor(QStringLiteral("#7DA6C6"))},
            {QStringLiteral("SQL"), QColor(QStringLiteral("#66B7A7"))},
            {QStringLiteral("ENV"), QColor(QStringLiteral("#83BB76"))},
            {QStringLiteral("DKR"), QColor(QStringLiteral("#64A9D7"))},
            {QStringLiteral("PY"), QColor(QStringLiteral("#E6C64A"))},
        }};
        for (const auto& [badge, color] : badges) {
            const QString iconName = QStringLiteral("code:") + badge;
            cachedIcon(iconName, iconName, color);
        }
    }

    QIcon cachedIcon(const QString& key, const QString& iconName, const QColor& accent) const {
        const auto existing = m_icons.constFind(key);
        if (existing != m_icons.cend()) {
            return existing.value();
        }

        const QIcon icon = forgeIcon(iconName, m_foreground, accent);
        m_icons.insert(key, icon);
        return icon;
    }

    QColor m_foreground;
    QColor m_accent;
    mutable QMutex m_mutex;
    mutable QHash<QString, QIcon> m_icons;
};

}  // namespace

MainWindow::MainWindow(forge::core::ActionManager* actions,
                       forge::core::ServiceSupervisor* supervisor,
                       QWidget* parent)
    : QMainWindow(parent)
    , m_actions(actions)
    , m_supervisor(supervisor)
{
    setWindowTitle(QStringLiteral("%1 %2")
                   .arg(QLatin1String(FORGE_PRODUCT_NAME),
                        QLatin1String(FORGE_VERSION)));
    setWindowIcon(forgeIcon(QStringLiteral("forge"), QColor(QStringLiteral("#DFE1E5")),
                            QColor(QStringLiteral("#4D78CC"))));
    setObjectName(QStringLiteral("ForgeMainWindow"));
    resize(1440, 900);
    setMinimumSize(900, 600);

    setupActions();
    setupToolWindows();
    setupMenus();
    setupToolbars();
    setupStatusBar();

    m_keymapManager = std::make_unique<forge::core::KeymapManager>(m_actions, this);
    if (!m_keymapManager->loadDefaultKeymap()) {
        statusBar()->showMessage(QStringLiteral("Could not load the default keymap."), 8000);
    }
    setupShortcuts();

    auto* settings = forge::core::Settings::instance();
    const QString theme = settings
        ? settings->value(QStringLiteral("ui.theme"), QStringLiteral("Graphite")).toString()
        : QStringLiteral("Graphite");
    applyTheme(theme);

    if (settings) {
        const QByteArray savedState = QByteArray::fromBase64(
            settings->value(QStringLiteral("window.state")).toString().toLatin1());
        const QByteArray savedGeometry = QByteArray::fromBase64(
            settings->value(QStringLiteral("window.geometry")).toString().toLatin1());
        if (!savedState.isEmpty() && !restoreState(savedState, kWindowLayoutVersion)) {
            qWarning() << "[MainWindow] Saved window layout could not be restored.";
        }
        if (!savedGeometry.isEmpty() && !restoreGeometry(savedGeometry)) {
            qWarning() << "[MainWindow] Saved window geometry could not be restored.";
        }

        const QString previousProject =
            settings->value(QStringLiteral("project.lastPath")).toString();
        if (!previousProject.isEmpty() && QDir(previousProject).exists()) {
            setProjectRoot(previousProject);
        }
    }

    setupServices();
    connect(m_supervisor, &forge::core::ServiceSupervisor::allServicesStopped, this, [this]() {
        if (m_servicesStopping) {
            m_servicesStopping = false;
            close();
        }
    });
    qInfo() << "[MainWindow] IDE shell initialized.";
}

MainWindow::~MainWindow() {
    if (m_projectTree) {
        m_projectTree->setModel(nullptr);
    }
    delete m_fileSystemModel;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    for (int index = m_editorTabs->count() - 1; index >= 0; --index) {
        m_editorTabs->setCurrentIndex(index);
        QPlainTextEdit* editor = currentEditor();
        if (editor && editor->document()->isModified()) {
            const QMessageBox::StandardButton result = QMessageBox::warning(
                this,
                QStringLiteral("Unsaved changes"),
                QStringLiteral("Save changes to %1?")
                    .arg(m_editorTabs->tabText(index).remove(QLatin1Char('*'))),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                QMessageBox::Save);
            if (result == QMessageBox::Cancel) {
                event->ignore();
                return;
            }
            if (result == QMessageBox::Save && !saveDocument(editor)) {
                event->ignore();
                return;
            }
        }
    }

    if (auto* settings = forge::core::Settings::instance()) {
        settings->setValue(QStringLiteral("window.state"),
                           QString::fromLatin1(saveState(kWindowLayoutVersion).toBase64()));
        settings->setValue(QStringLiteral("window.geometry"),
                           QString::fromLatin1(saveGeometry().toBase64()));
        settings->save();
    }
    if (!m_supervisor->allStopped()) {
        m_servicesStopping = true;
        setEnabled(false);
        statusBar()->showMessage(QStringLiteral("Stopping background services..."));
        event->ignore();
        m_supervisor->stopAll();
        return;
    }
    for (QProcess* process : std::as_const(m_terminalProcesses)) {
        if (process && process->state() != QProcess::NotRunning) {
            process->terminate();
            if (!process->waitForFinished(1000)) {
                process->kill();
                process->waitForFinished(1000);
            }
        }
    }
    if (m_commandProcess && m_commandProcess->state() != QProcess::NotRunning) {
        m_commandProcess->terminate();
        if (!m_commandProcess->waitForFinished(1000)) {
            m_commandProcess->kill();
            m_commandProcess->waitForFinished(1000);
        }
    }
    event->accept();
}

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
                dock->show();
                dock->raise();
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
                dock->show();
                dock->raise();
            }
        });
    m_actions->registerAction(
        QStringLiteral("forge.window.terminal"), QStringLiteral("Terminal"), [this]() {
            if (auto* dock = findChild<QDockWidget*>(QStringLiteral("TerminalToolWindow"))) {
                dock->show();
                dock->raise();
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
        if (terminalAction) {
            for (QPlainTextEdit* terminalView : std::as_const(m_terminalViews)) {
                terminalView->addAction(action);
            }
        }
    }
}

void MainWindow::setupMenus() {
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(m_actions->action(QStringLiteral("forge.file.new")));
    fileMenu->addAction(m_actions->action(QStringLiteral("forge.file.open")));
    fileMenu->addAction(m_actions->action(QStringLiteral("forge.file.openProject")));
    fileMenu->addSeparator();
    fileMenu->addAction(m_actions->action(QStringLiteral("forge.file.save")));
    fileMenu->addAction(m_actions->action(QStringLiteral("forge.file.close")));
    fileMenu->addSeparator();
    fileMenu->addAction(m_actions->action(QStringLiteral("forge.app.quit")));

    QMenu* editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
    editMenu->addAction(m_actions->action(QStringLiteral("forge.edit.undo")));
    editMenu->addAction(m_actions->action(QStringLiteral("forge.edit.redo")));
    editMenu->addSeparator();
    editMenu->addAction(m_actions->action(QStringLiteral("forge.edit.cut")));
    editMenu->addAction(m_actions->action(QStringLiteral("forge.edit.copy")));
    editMenu->addAction(m_actions->action(QStringLiteral("forge.edit.paste")));
    editMenu->addAction(m_actions->action(QStringLiteral("forge.edit.selectAll")));

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    QMenu* themeMenu = viewMenu->addMenu(QStringLiteral("Theme"));
    themeMenu->addAction(m_actions->action(QStringLiteral("forge.theme.graphite")));
    themeMenu->addAction(m_actions->action(QStringLiteral("forge.theme.daylight")));
    viewMenu->addSeparator();
    viewMenu->addAction(m_actions->action(QStringLiteral("forge.window.projectTree")));
    viewMenu->addAction(m_actions->action(QStringLiteral("forge.window.terminal")));

    QMenu* navigateMenu = menuBar()->addMenu(QStringLiteral("&Navigate"));
    navigateMenu->addAction(m_actions->action(QStringLiteral("forge.navigate.find")));
    navigateMenu->addAction(m_actions->action(QStringLiteral("forge.navigate.findNext")));
    navigateMenu->addAction(m_actions->action(QStringLiteral("forge.navigate.goToLine")));
    navigateMenu->addSeparator();
    navigateMenu->addAction(m_actions->action(QStringLiteral("forge.navigate.goToFile")));
    navigateMenu->addAction(m_actions->action(QStringLiteral("forge.navigate.recentFiles")));

    QMenu* codeMenu = menuBar()->addMenu(QStringLiteral("&Code"));
    QAction* codeUnavailable = codeMenu->addAction(
        QStringLiteral("PHP completion and inspections are not implemented yet"));
    codeUnavailable->setEnabled(false);

    QMenu* refactorMenu = menuBar()->addMenu(QStringLiteral("&Refactor"));
    QAction* refactorUnavailable =
        refactorMenu->addAction(QStringLiteral("Safe PHP refactoring is not implemented yet"));
    refactorUnavailable->setEnabled(false);

    QMenu* runMenu = menuBar()->addMenu(QStringLiteral("&Run"));
    runMenu->addAction(m_actions->action(QStringLiteral("forge.run.run")));
    runMenu->addAction(m_actions->action(QStringLiteral("forge.run.debug")));
    runMenu->addAction(m_actions->action(QStringLiteral("forge.run.profile")));
    runMenu->addAction(m_actions->action(QStringLiteral("forge.debug.stepOver")));
    runMenu->addAction(m_actions->action(QStringLiteral("forge.debug.stepInto")));
    runMenu->addAction(m_actions->action(QStringLiteral("forge.debug.stepOut")));
    runMenu->addAction(m_actions->action(QStringLiteral("forge.run.stop")));

    QMenu* toolsMenu = menuBar()->addMenu(QStringLiteral("&Tools"));
    toolsMenu->addAction(m_actions->action(QStringLiteral("forge.window.terminal")));
    toolsMenu->addAction(m_actions->action(QStringLiteral("forge.terminal.new")));
    toolsMenu->addAction(m_actions->action(QStringLiteral("forge.terminal.clear")));
    toolsMenu->addAction(m_actions->action(QStringLiteral("forge.window.settings")));
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_actions->action(QStringLiteral("forge.window.plugins")));

    QMenu* gitMenu = menuBar()->addMenu(QStringLiteral("&Git"));
    gitMenu->addAction(m_actions->action(QStringLiteral("forge.vcs.status")));

    QMenu* windowMenu = menuBar()->addMenu(QStringLiteral("&Window"));
    windowMenu->addAction(m_actions->action(QStringLiteral("forge.window.restoreLayout")));
    windowMenu->addAction(m_actions->action(QStringLiteral("forge.window.settings")));

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    QAction* about = helpMenu->addAction(QStringLiteral("About Forge IDE"));
    connect(about, &QAction::triggered, this, [this]() {
        QMessageBox::about(
            this, QStringLiteral("About Forge IDE"),
            QStringLiteral("%1 %2\nNative PHP and Laravel IDE in development.")
                .arg(QLatin1String(FORGE_PRODUCT_NAME), QLatin1String(FORGE_VERSION)));
    });
}

void MainWindow::setupToolbars() {
    auto configureToolbar = [](QToolBar* toolbar, const QString& name) {
        toolbar->setObjectName(name);
        toolbar->setMovable(false);
        toolbar->setFloatable(false);
        toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
        toolbar->setIconSize(QSize(18, 18));
    };

    auto* toolbar = addToolBar(QStringLiteral("Main"));
    configureToolbar(toolbar, QStringLiteral("MainToolbar"));
    toolbar->addAction(m_actions->action(QStringLiteral("forge.file.openProject")));
    toolbar->addAction(m_actions->action(QStringLiteral("forge.file.open")));
    toolbar->addAction(m_actions->action(QStringLiteral("forge.file.save")));
    toolbar->addSeparator();
    m_projectLabel = new QLabel(QStringLiteral("No project"), toolbar);
    m_projectLabel->setMinimumWidth(120);
    toolbar->addWidget(m_projectLabel);
    toolbar->addSeparator();
    m_runConfiguration = new QComboBox(toolbar);
    m_runConfiguration->setObjectName(QStringLiteral("RunConfigurationSelector"));
    m_runConfiguration->addItem(QStringLiteral("Current File"));
    m_runConfiguration->setMinimumWidth(145);
    m_runConfiguration->setToolTip(
        QStringLiteral("Run configurations will be available in milestone M6."));
    toolbar->addWidget(m_runConfiguration);
    toolbar->addAction(m_actions->action(QStringLiteral("forge.run.run")));
    toolbar->addAction(m_actions->action(QStringLiteral("forge.run.debug")));
    toolbar->addAction(m_actions->action(QStringLiteral("forge.run.stop")));
    toolbar->addSeparator();
    auto* searchAction =
        toolbar->addAction(forgeIcon(QStringLiteral("search"), QColor(QStringLiteral("#DFE1E5")),
                                     QColor(QStringLiteral("#4D78CC"))),
                           QStringLiteral("Go to File"));
    searchAction->setToolTip(QStringLiteral("Go to File (Ctrl+Shift+N)"));
    connect(searchAction, &QAction::triggered, this, &MainWindow::goToFile);
    toolbar->addAction(m_actions->action(QStringLiteral("forge.window.settings")));

    auto* leftStripe = new QToolBar(QStringLiteral("Workspace"), this);
    addToolBar(Qt::LeftToolBarArea, leftStripe);
    configureToolbar(leftStripe, QStringLiteral("WorkspaceToolStripe"));
    leftStripe->addAction(m_actions->action(QStringLiteral("forge.window.projectTree")));
    leftStripe->addAction(m_actions->action(QStringLiteral("forge.vcs.status")));
}

void MainWindow::setupToolWindows() {
    m_centerStack = new QStackedWidget(this);
    m_welcomePage = new QWidget(m_centerStack);
    auto* welcomeLayout = new QVBoxLayout(m_welcomePage);
    welcomeLayout->setAlignment(Qt::AlignCenter);

    auto* title = new QLabel(QStringLiteral(FORGE_PRODUCT_NAME), m_welcomePage);
    title->setObjectName(QStringLiteral("welcomeTitle"));
    title->setAlignment(Qt::AlignCenter);
    auto* subtitle = new QLabel(
        QStringLiteral("A native workspace for PHP and Laravel\n"
                       "Milestone 1: IDE shell. Editor intelligence is still in development."),
        m_welcomePage);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setWordWrap(true);

    auto* openProjectButton = new QPushButton(QStringLiteral("Open Project"), m_welcomePage);
    openProjectButton->setObjectName(QStringLiteral("openProjectButton"));
    openProjectButton->setMinimumWidth(180);
    connect(openProjectButton, &QPushButton::clicked, this, &MainWindow::openProject);
    auto* openFileButton = new QPushButton(QStringLiteral("Open File"), m_welcomePage);
    openFileButton->setObjectName(QStringLiteral("openFileButton"));
    openFileButton->setMinimumWidth(180);
    connect(openFileButton, &QPushButton::clicked, this,
            [this]() { m_actions->action(QStringLiteral("forge.file.open"))->trigger(); });

    welcomeLayout->addWidget(title);
    welcomeLayout->addSpacing(10);
    welcomeLayout->addWidget(subtitle);
    welcomeLayout->addSpacing(20);
    welcomeLayout->addWidget(openProjectButton, 0, Qt::AlignHCenter);
    welcomeLayout->addWidget(openFileButton, 0, Qt::AlignHCenter);

    m_centerStack->addWidget(m_welcomePage);
    m_editorTabs = new QTabWidget(m_centerStack);
    m_editorTabs->setDocumentMode(true);
    m_editorTabs->setTabsClosable(true);
    m_editorTabs->setMovable(true);
    connect(m_editorTabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeDocument);
    connect(m_editorTabs, &QTabWidget::currentChanged, this, [this](int) { updateEditorStatus(); });
    m_centerStack->addWidget(m_editorTabs);
    setCentralWidget(m_centerStack);

    m_fileSystemModel = new ForgeFileSystemModel(this);
    m_fileIconProvider = std::make_unique<ForgeFileIconProvider>(QColor(QStringLiteral("#DFE1E5")),
                                                                 QColor(QStringLiteral("#4D78CC")));
    m_fileSystemModel->setIconProvider(m_fileIconProvider.get());
    m_fileSystemModel->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_projectTree = new QTreeView(this);
    m_projectTree->setModel(m_fileSystemModel);
    m_projectTree->setMouseTracking(true);
    m_projectTree->setHeaderHidden(true);
    m_projectTree->hideColumn(1);
    m_projectTree->hideColumn(2);
    m_projectTree->hideColumn(3);
    m_projectTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(m_projectTree, &QTreeView::doubleClicked, this, &MainWindow::openFile);

    auto* projectDock = new QDockWidget(QStringLiteral("Project"), this);
    projectDock->setObjectName(QStringLiteral("ProjectToolWindow"));
    projectDock->setWidget(m_projectTree);
    addDockWidget(Qt::LeftDockWidgetArea, projectDock);

    auto* terminalDock = new QDockWidget(QStringLiteral("Terminal"), this);
    terminalDock->setObjectName(QStringLiteral("TerminalToolWindow"));
    auto* terminalPanel = new QWidget(terminalDock);
    auto* terminalLayout = new QVBoxLayout(terminalPanel);
    terminalLayout->setContentsMargins(4, 4, 4, 4);
    terminalLayout->setSpacing(4);
    auto* terminalControls = new QHBoxLayout;
    terminalControls->setContentsMargins(0, 0, 0, 0);
    auto* newTerminalButton = new QPushButton(QStringLiteral("+ New Terminal"), terminalPanel);
    newTerminalButton->setToolTip(QStringLiteral("Open another PowerShell session"));
    terminalControls->addWidget(newTerminalButton);
    terminalControls->addStretch();
    terminalLayout->addLayout(terminalControls);
    m_terminalTabs = new QTabWidget(terminalPanel);
    m_terminalTabs->setObjectName(QStringLiteral("TerminalSessionTabs"));
    m_terminalTabs->setTabsClosable(false);
    terminalLayout->addWidget(m_terminalTabs);
    connect(newTerminalButton, &QPushButton::clicked, this,
            [this]() { createTerminalSession(); });
    connect(m_terminalTabs, &QTabWidget::currentChanged, this,
            [this](int index) { setActiveTerminalSession(index); });
    connect(m_terminalTabs, &QTabWidget::tabCloseRequested, this,
            [this](int index) { closeTerminalSession(index); });
    terminalDock->setWidget(terminalPanel);
    addDockWidget(Qt::BottomDockWidgetArea, terminalDock);
    connect(terminalDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible) {
            startTerminal();
            m_terminalOutput->setFocus();
        }
    });
    createTerminalSession();
    terminalDock->raise();
}

void MainWindow::setupStatusBar() {
    auto* encodingLabel = new QLabel(QStringLiteral("UTF-8"), this);
    encodingLabel->setToolTip(QStringLiteral("Files are saved as UTF-8."));
    auto* lineEndingLabel = new QLabel(QStringLiteral("LF"), this);
    lineEndingLabel->setToolTip(QStringLiteral("Files are saved with LF line endings."));
    m_caretLabel = new QLabel(QStringLiteral("Ln 1, Col 1"), this);
    m_fileTypeLabel = new QLabel(QStringLiteral("No file"), this);
    statusBar()->addPermanentWidget(encodingLabel);
    statusBar()->addPermanentWidget(lineEndingLabel);
    statusBar()->addPermanentWidget(m_fileTypeLabel);
    statusBar()->addPermanentWidget(m_caretLabel);
    m_statusLabel = new QLabel(QStringLiteral("Services: starting..."), this);
    statusBar()->addPermanentWidget(m_statusLabel);
    statusBar()->showMessage(QStringLiteral("Ready — open a project to get started."));
}

void MainWindow::createTerminalSession()
{
    if (!m_terminalTabs) {
        return;
    }

    auto* output = new TerminalOutputView(m_terminalTabs);
    output->setObjectName(QStringLiteral("TerminalOutput"));
    output->setMaximumBlockCount(5000);
    output->setLineWrapMode(QPlainTextEdit::NoWrap);
    auto* process = new QProcess(this);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    m_terminalViews.append(output);
    m_terminalProcesses.append(process);

    output->setCommandHandler(
        [this, output](const QString& command) { submitTerminalCommand(command, output); });
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process, output]() {
        appendTerminalOutput(QString::fromLocal8Bit(process->readAllStandardOutput()), output);
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, process, output]() {
        appendTerminalOutput(QString::fromLocal8Bit(process->readAllStandardError()), output);
    });
    connect(process, &QProcess::started, this, [this, process, output]() {
        appendTerminalOutput(QStringLiteral("Windows PowerShell started.\r\n"), output);
        const QStringList pendingCommands = m_pendingTerminalCommands.take(process);
        for (const QString& command : pendingCommands) {
            process->write(command.toLocal8Bit() + "\r\n");
        }
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, output](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    appendTerminalOutput(
                        QStringLiteral("Could not start PowerShell: %1\r\n")
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

    const int tabIndex = m_terminalTabs->addTab(
        output, QStringLiteral("PowerShell %1").arg(m_terminalTabs->count() + 1));
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

void MainWindow::closeTerminalSession(int index)
{
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
            m_terminalTabs->setTabText(tab, QStringLiteral("PowerShell %1").arg(tab + 1));
        }
        setActiveTerminalSession(m_terminalTabs->currentIndex());
    }
}

void MainWindow::setActiveTerminalSession(int index)
{
    if (index < 0 || index >= m_terminalViews.size() || index >= m_terminalProcesses.size()) {
        m_terminalOutput = nullptr;
        m_terminalProcess = nullptr;
        return;
    }
    m_terminalOutput = m_terminalViews.at(index);
    m_terminalProcess = m_terminalProcesses.at(index);
}

void MainWindow::startTerminal(QProcess* process, QPlainTextEdit* output)
{
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
    process->start(
        QStringLiteral("powershell.exe"),
        {QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"), QStringLiteral("-NoExit")});
}

void MainWindow::submitTerminalCommand(const QString& command, QPlainTextEdit* output)
{
    if (!output) {
        output = m_terminalOutput;
    }
    if (command.isEmpty() || !output) {
        return;
    }

    const int index = m_terminalViews.indexOf(output);
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
    if (process->write(command.toLocal8Bit() + "\r\n") < 0) {
        appendTerminalOutput(
            QStringLiteral("Could not send command: %1\r\n").arg(process->errorString()), output);
    }
}

void MainWindow::appendTerminalOutput(const QString& text, QPlainTextEdit* output)
{
    if (!output) {
        output = m_terminalOutput;
    }
    if (!output || text.isEmpty()) {
        return;
    }
    static_cast<TerminalOutputView*>(output)->appendOutput(text);
}

void MainWindow::findInCurrentDocument()
{
    QPlainTextEdit* editor = currentEditor();
    if (!editor) {
        statusBar()->showMessage(QStringLiteral("Open a file before searching."), 3000);
        return;
    }

    bool accepted = false;
    const QString query = QInputDialog::getText(
        this, QStringLiteral("Find in Current File"), QStringLiteral("Search for:"),
        QLineEdit::Normal, m_lastFindQuery, &accepted);
    if (!accepted || query.isEmpty()) {
        return;
    }

    m_lastFindQuery = query;
    if (!editor->find(query)) {
        QTextCursor cursor = editor->textCursor();
        cursor.movePosition(QTextCursor::Start);
        editor->setTextCursor(cursor);
        if (!editor->find(query)) {
            statusBar()->showMessage(QStringLiteral("No match found for \"%1\".").arg(query), 4000);
        }
    }
    editor->setFocus();
}

void MainWindow::findNextInCurrentDocument()
{
    QPlainTextEdit* editor = currentEditor();
    if (!editor) {
        statusBar()->showMessage(QStringLiteral("Open a file before searching."), 3000);
        return;
    }
    if (m_lastFindQuery.isEmpty()) {
        findInCurrentDocument();
        return;
    }
    if (!editor->find(m_lastFindQuery)) {
        QTextCursor cursor = editor->textCursor();
        cursor.movePosition(QTextCursor::Start);
        editor->setTextCursor(cursor);
        if (!editor->find(m_lastFindQuery)) {
            statusBar()->showMessage(
                QStringLiteral("No match found for \"%1\".").arg(m_lastFindQuery), 4000);
        }
    }
    editor->setFocus();
}

void MainWindow::goToLine()
{
    QPlainTextEdit* editor = currentEditor();
    if (!editor) {
        statusBar()->showMessage(QStringLiteral("Open a file before navigating to a line."), 3000);
        return;
    }

    bool accepted = false;
    const int line = QInputDialog::getInt(
        this, QStringLiteral("Go to Line"), QStringLiteral("Line number:"),
        editor->textCursor().blockNumber() + 1, 1,
        editor->document()->blockCount(), 1, &accepted);
    if (!accepted) {
        return;
    }

    QTextCursor cursor(editor->document()->findBlockByNumber(line - 1));
    editor->setTextCursor(cursor);
    editor->centerCursor();
    editor->setFocus();
}

void MainWindow::goToFile() {
    if (m_projectRoot.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Open a project before searching its files."),
                                 4000);
        return;
    }

    QStringList files;
    collectProjectFiles(m_projectRoot, files);

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Go to File"));
    dialog.setMinimumSize(640, 440);
    auto* layout = new QVBoxLayout(&dialog);
    auto* query = new QLineEdit(&dialog);
    query->setPlaceholderText(QStringLiteral("Type a file name or path..."));
    auto* results = new QListWidget(&dialog);
    results->setMouseTracking(true);
    results->setIconSize(QSize(18, 18));
    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Open)->setEnabled(false);
    layout->addWidget(query);
    layout->addWidget(results);
    layout->addWidget(buttons);

    auto updateResults = [this, &files, query, results, buttons]() {
        const QString text = query->text().trimmed();
        results->clear();
        int visibleCount = 0;
        for (const QString& path : files) {
            const QString relative = QDir(m_projectRoot).relativeFilePath(path);
            if (!text.isEmpty() && !relative.contains(text, Qt::CaseInsensitive) &&
                !QFileInfo(path).fileName().contains(text, Qt::CaseInsensitive)) {
                continue;
            }

            auto* item =
                new QListWidgetItem(m_fileIconProvider->icon(QFileInfo(path)), relative, results);
            item->setData(Qt::UserRole, path);
            item->setToolTip(
                QStringLiteral("%1\nLanguage: %2").arg(path, fileLanguageName(QFileInfo(path))));
            if (++visibleCount == 150) {
                break;
            }
        }
        if (results->count() > 0) {
            results->setCurrentRow(0);
        }
        buttons->button(QDialogButtonBox::Open)->setEnabled(results->count() > 0);
    };
    connect(query, &QLineEdit::textChanged, &dialog, updateResults);
    connect(results, &QListWidget::itemSelectionChanged, &dialog, [results, buttons]() {
        buttons->button(QDialogButtonBox::Open)->setEnabled(results->currentItem() != nullptr);
    });
    connect(results, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    updateResults();
    query->setFocus();

    if (dialog.exec() == QDialog::Accepted && results->currentItem()) {
        openFilePath(results->currentItem()->data(Qt::UserRole).toString());
    }

    if (files.size() >= 15000) {
        statusBar()->showMessage(
            QStringLiteral("File search is limited to the first 15,000 files; vendor, "
                           "node_modules, build, .git, and .idea are excluded."),
            7000);
    }
}

void MainWindow::showRecentFiles() {
    QStringList existingFiles;
    for (const QString& path : std::as_const(m_recentFiles)) {
        if (QFileInfo::exists(path)) {
            existingFiles.append(path);
        }
    }

    if (existingFiles.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("No recently opened files."), 3000);
        return;
    }

    bool accepted = false;
    const QString path =
        QInputDialog::getItem(this, QStringLiteral("Recent Files"), QStringLiteral("Open file:"),
                              existingFiles, 0, false, &accepted);
    if (accepted && !path.isEmpty()) {
        openFilePath(path);
    }
}

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

void MainWindow::showGitStatus() {
    if (m_projectRoot.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Open a project before checking Git status."),
                                 4000);
        return;
    }
    const QString gitExecutable = QStandardPaths::findExecutable(QStringLiteral("git.exe"));
    if (gitExecutable.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Git Not Found"),
                             QStringLiteral("Add git.exe to PATH, then restart Forge IDE."));
        return;
    }
    if (auto* dock = findChild<QDockWidget*>(QStringLiteral("TerminalToolWindow"))) {
        dock->show();
        dock->raise();
    }
    runExternalCommand(gitExecutable,
                       {QStringLiteral("-C"), m_projectRoot, QStringLiteral("status"),
                        QStringLiteral("--short"), QStringLiteral("--branch")},
                       QStringLiteral("Git status"));
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
            appendTerminalOutput(QString::fromLocal8Bit(m_commandProcess->readAllStandardOutput()));
        });
        connect(m_commandProcess, &QProcess::readyReadStandardError, this, [this]() {
            appendTerminalOutput(QString::fromLocal8Bit(m_commandProcess->readAllStandardError()));
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
}

void MainWindow::setupServices()
{
    connect(m_supervisor, &forge::core::ServiceSupervisor::serviceStateChanged,
            this, &MainWindow::onServiceStateChanged);
    connect(m_supervisor, &forge::core::ServiceSupervisor::serviceStarted, this,
            [this](const QString& key) {
                if (key != QStringLiteral("phpintel")) {
                    return;
                }
                QProcess* process = m_supervisor->process(key);
                if (!process) {
                    qWarning() << "[MainWindow] phpintel process is not available after startup.";
                    m_statusLabel->setText(QStringLiteral("phpintel: unavailable"));
                    return;
                }
                if (!m_phpIntelClient) {
                    m_phpIntelClient = std::make_unique<forge::core::RpcClient>(process, this);
                }
                m_phpIntelClient->sendRequest(QStringLiteral("$/ping"), {},
                                              [this](const QJsonObject& result,
                                                     const QJsonObject& error) {
                                                  onPingResponse(result, error);
                                              });
            });

    const QString phpIntelBin =
        QCoreApplication::applicationDirPath() + QStringLiteral("/phpintel.exe");
    if (!QFileInfo::exists(phpIntelBin)) {
        qWarning() << "[MainWindow] phpintel binary not found at" << phpIntelBin;
        m_statusLabel->setText(QStringLiteral("phpintel: not built"));
        return;
    }

    forge::core::ServiceSupervisor::ServiceConfig cfg;
    cfg.key = QStringLiteral("phpintel");
    cfg.executable = phpIntelBin;
    cfg.arguments = {QStringLiteral("--stdio")};
    m_supervisor->registerService(cfg);
}

void MainWindow::openProject()
{
    const QString path = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Open Project"), m_projectRoot);
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

    m_projectRoot = info.absoluteFilePath();
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
    const QString command = QStringLiteral("Set-Location -LiteralPath '%1'").arg(escapedRoot);
    for (QProcess* process : std::as_const(m_terminalProcesses)) {
        if (process && process->state() != QProcess::NotRunning) {
            process->write(command.toLocal8Bit() + "\r\n");
        }
    }
    statusBar()->showMessage(QStringLiteral("Project opened: %1").arg(m_projectRoot), 5000);
    m_actions->action(QStringLiteral("forge.window.projectTree"))->trigger();
}

void MainWindow::openFile(const QModelIndex& index)
{
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
            m_centerStack->setCurrentWidget(m_editorTabs);
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
    const QByteArray contents = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        QMessageBox::critical(
            this, QStringLiteral("Open File"),
            QStringLiteral("Could not read %1 completely:\n%2")
                .arg(info.fileName(), file.errorString()));
        return;
    }
    forge::core::DecodedText decoded;
    QString decodeError;
    if (!forge::core::TextFileCodec::decode(contents, &decoded, &decodeError)) {
        QMessageBox::critical(this, QStringLiteral("Open File"),
                              QStringLiteral("Could not decode %1:\n%2")
                                  .arg(info.fileName(), decodeError));
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
    editor->setProperty("textEncoding", static_cast<int>(decoded.encoding));
    editor->setObjectName(QStringLiteral("ForgeCodeEditor"));
    editor->setPlainText(decoded.text);
    storeLineEndings(editor->document(), decoded.lineEndings);
    auto* highlighter = new ForgeSyntaxHighlighter(
        editor->document(),
        info.fileName().toLower().endsWith(QStringLiteral(".blade.php")) ? QStringLiteral("blade")
                                                                         : info.suffix().toLower(),
        editor);
    editor->setSyntaxHighlighter(highlighter);
    applyEditorTheme(editor);
    editor->document()->setModified(false);
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this,
            [this]() { updateEditorStatus(); });
    const int tabIndex = m_editorTabs->addTab(
        editor, m_fileIconProvider->icon(info), info.fileName());
    m_editorTabs->setTabToolTip(
        tabIndex, QStringLiteral("%1\nLanguage: %2")
                      .arg(info.absoluteFilePath(), fileLanguageName(info)));
    m_editorTabs->setCurrentIndex(tabIndex);
    connect(editor->document(), &QTextDocument::modificationChanged,
            this, [this, editor](bool modified) {
                const int index = m_editorTabs->indexOf(editor);
                if (index >= 0) {
                    const QString title = QFileInfo(editor->property("filePath").toString())
                                              .fileName();
                    m_editorTabs->setTabText(index, modified ? title + QLatin1Char('*') : title);
                }
            });
    m_centerStack->setCurrentWidget(m_editorTabs);
}

void MainWindow::createUntitledDocument()
{
    auto* editor = new ForgeCodeEditor(m_editorTabs);
    editor->setProperty("filePath", QString());
    editor->setProperty("forgeSourceEditor", true);
    editor->setProperty("textEncoding", static_cast<int>(forge::core::TextEncoding::Utf8));
    editor->setObjectName(QStringLiteral("ForgeCodeEditor"));
    auto* highlighter = new ForgeSyntaxHighlighter(editor->document(), QString(), editor);
    editor->setSyntaxHighlighter(highlighter);
    applyEditorTheme(editor);
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this,
            [this]() { updateEditorStatus(); });
    const int index = m_editorTabs->addTab(editor, QStringLiteral("Untitled"));
    m_editorTabs->setCurrentIndex(index);
    m_centerStack->setCurrentWidget(m_editorTabs);
    editor->setFocus();
}

bool MainWindow::saveCurrentDocument()
{
    QPlainTextEdit* editor = currentEditor();
    if (!editor) {
        statusBar()->showMessage(QStringLiteral("There is no open document to save."), 4000);
        return false;
    }
    return saveDocument(editor);
}

bool MainWindow::saveDocument(QPlainTextEdit* editor)
{
    QString path = editor->property("filePath").toString();
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, QStringLiteral("Save File"), m_projectRoot);
        if (path.isEmpty()) {
            return false;
        }
    }

    const auto encoding = static_cast<forge::core::TextEncoding>(
        editor->property("textEncoding").toInt());
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
        QMessageBox::critical(this, QStringLiteral("Save File"),
                              QStringLiteral("Could not save %1:\n%2")
                                  .arg(path, file.errorString()));
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
        QMessageBox::critical(this, QStringLiteral("Save File"),
                              QStringLiteral("Could not complete saving %1:\n%2")
                                  .arg(path, file.errorString()));
        return false;
    }

    editor->setProperty("filePath", path);
    editor->document()->setModified(false);
    const int index = m_editorTabs->indexOf(editor);
    if (index >= 0) {
        const QFileInfo info(path);
        m_editorTabs->setTabText(index, info.fileName());
        m_editorTabs->setTabToolTip(index, info.absoluteFilePath());
    }
    statusBar()->showMessage(QStringLiteral("Saved %1").arg(path), 3000);
    return true;
}

void MainWindow::closeDocument(int index)
{
    auto* editor = qobject_cast<QPlainTextEdit*>(m_editorTabs->widget(index));
    if (!editor) {
        return;
    }
    if (editor->document()->isModified()) {
        const QMessageBox::StandardButton result = QMessageBox::warning(
            this, QStringLiteral("Unsaved changes"),
            QStringLiteral("Save changes to %1?")
                .arg(m_editorTabs->tabText(index).remove(QLatin1Char('*'))),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
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
}

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
    m_themeName = root.value(QStringLiteral("name")).toString(name);
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
         {QStringLiteral("forge.run.debug"), QStringLiteral("run")},
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
        settings->setValue(QStringLiteral("ui.theme"),
                           root.value(QStringLiteral("name")).toString(name));
    }
    statusBar()->showMessage(QStringLiteral("Theme: %1").arg(name), 2500);
}

void MainWindow::showSettings() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Forge IDE Settings"));
    dialog.setMinimumWidth(360);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout();
    auto* theme = new QComboBox(&dialog);
    theme->addItems({QStringLiteral("Graphite"), QStringLiteral("Daylight")});
    const QString currentTheme =
        forge::core::Settings::instance()
            ? forge::core::Settings::instance()
                  ->value(QStringLiteral("ui.theme"), QStringLiteral("Graphite"))
                  .toString()
            : QStringLiteral("Graphite");
    theme->setCurrentText(currentTheme);
    form->addRow(QStringLiteral("Appearance:"), theme);
    auto* keymap = new QLabel(QStringLiteral("Default keymap"), &dialog);
    form->addRow(QStringLiteral("Keymap:"), keymap);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() == QDialog::Accepted) {
        applyTheme(theme->currentText());
    }
}

void MainWindow::showPluginManager() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Forge IDE Plugin Manager"));
    dialog.resize(560, 420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* tabs = new QTabWidget(&dialog);

    const QString pluginDirectory =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
            .filePath(QStringLiteral("plugins"));
    auto* installedPage = new QWidget(tabs);
    auto* installedLayout = new QVBoxLayout(installedPage);
    auto* installedList = new QListWidget(installedPage);
    QDir plugins(pluginDirectory);
    if (!plugins.exists() && !QDir().mkpath(pluginDirectory)) {
        QMessageBox::warning(
            this, QStringLiteral("Plugin Manager"),
            QStringLiteral("Could not create the plugin directory:\n%1").arg(pluginDirectory));
    }
    const QFileInfoList pluginFolders =
        plugins.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& folder : pluginFolders) {
        QFile manifest(QDir(folder.absoluteFilePath()).filePath(QStringLiteral("plugin.json")));
        if (!manifest.open(QIODevice::ReadOnly)) {
            installedList->addItem(
                QStringLiteral("%1: unable to read plugin.json").arg(folder.fileName()));
            continue;
        }
        QJsonParseError parseError;
        const QJsonDocument manifestDocument =
            QJsonDocument::fromJson(manifest.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !manifestDocument.isObject()) {
            installedList->addItem(
                QStringLiteral("%1: invalid plugin.json").arg(folder.fileName()));
            continue;
        }
        const QJsonObject metadata = manifestDocument.object();
        const QString name = metadata.value(QStringLiteral("name")).toString();
        const QString version = metadata.value(QStringLiteral("version")).toString();
        const QString id = metadata.value(QStringLiteral("id")).toString();
        if (!name.isEmpty() && !id.isEmpty()) {
            installedList->addItem(
                QStringLiteral("%1 %2 — %3 (not loaded)").arg(name, version, id));
        }
    }
    if (installedList->count() == 0) {
        installedList->addItem(QStringLiteral("No native plugins are installed."));
    }
    installedLayout->addWidget(installedList);
    auto* openDirectory = new QPushButton(QStringLiteral("Open Plugin Directory"), installedPage);
    connect(openDirectory, &QPushButton::clicked, &dialog, [this, pluginDirectory]() {
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(pluginDirectory))) {
            QMessageBox::warning(this, QStringLiteral("Plugin Manager"),
                                 QStringLiteral("Could not open the plugin directory."));
        }
    });
    installedLayout->addWidget(openDirectory, 0, Qt::AlignRight);
    tabs->addTab(installedPage, QStringLiteral("Installed"));

    auto* marketplacePage = new QWidget(tabs);
    auto* marketplaceLayout = new QVBoxLayout(marketplacePage);
    auto* marketplaceInfo = new QLabel(
        QStringLiteral("Online installation is not enabled yet. Forge requires a configured, "
                       "signed plugin catalog and ABI validation before it can download or load "
                       "native code. No plugin files are downloaded or executed here."),
        marketplacePage);
    marketplaceInfo->setWordWrap(true);
    marketplaceLayout->addWidget(marketplaceInfo);
    marketplaceLayout->addStretch();
    tabs->addTab(marketplacePage, QStringLiteral("Marketplace"));

    auto* languagesPage = new QWidget(tabs);
    auto* languagesLayout = new QVBoxLayout(languagesPage);
    auto* languageInfo = new QLabel(
        QStringLiteral("Built-in lexical highlighting and file recognition currently cover "
                       "PHP, Blade, JavaScript/TypeScript, HTML/XML, Vue, Python, SQL, JSON, "
                       "YAML, Markdown, CSS/SCSS, and common configuration files. "
                       "Language-server intelligence and downloadable language plugins are "
                       "not available yet."),
        languagesPage);
    languageInfo->setWordWrap(true);
    languagesLayout->addWidget(languageInfo);
    languagesLayout->addStretch();
    tabs->addTab(languagesPage, QStringLiteral("Languages"));

    layout->addWidget(tabs);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
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
    const QFont font(
        metrics.value(QStringLiteral("fontFamily")).toString(QStringLiteral("Consolas")),
        metrics.value(QStringLiteral("fontSize")).toInt(14));
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

void MainWindow::restoreDefaultLayout() {
    resizeDocks({findChild<QDockWidget*>(QStringLiteral("ProjectToolWindow"))}, {260},
                Qt::Horizontal);
    resizeDocks({findChild<QDockWidget*>(QStringLiteral("TerminalToolWindow"))}, {180},
                Qt::Vertical);
    findChild<QDockWidget*>(QStringLiteral("ProjectToolWindow"))->show();
    findChild<QDockWidget*>(QStringLiteral("TerminalToolWindow"))->show();
    statusBar()->showMessage(QStringLiteral("Default window layout restored."), 3000);
}

QPlainTextEdit* MainWindow::currentEditor() const
{
    return qobject_cast<QPlainTextEdit*>(m_editorTabs->currentWidget());
}

void MainWindow::updateEditorStatus() {
    QPlainTextEdit* editor = currentEditor();
    if (!editor) {
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

void MainWindow::onServiceStateChanged(const QString& key,
                                       forge::core::ServiceSupervisor::ServiceState state) {
    using State = forge::core::ServiceSupervisor::ServiceState;
    QString stateText;
    switch (state) {
    case State::Starting: stateText = QStringLiteral("starting"); break;
    case State::Running: stateText = QStringLiteral("running"); break;
    case State::Crashed: stateText = QStringLiteral("crashed"); break;
    case State::Disabled: stateText = QStringLiteral("disabled"); break;
    case State::Stopped: stateText = QStringLiteral("stopped"); break;
    }
    m_statusLabel->setText(QStringLiteral("%1: %2").arg(key, stateText));
    qInfo() << "[MainWindow] Service" << key << "->" << stateText;
}

void MainWindow::onPingResponse(const QJsonObject& result, const QJsonObject& error)
{
    if (!error.isEmpty()) {
        qWarning() << "[MainWindow] Ping error:" << error;
        m_statusLabel->setText(QStringLiteral("phpintel: handshake failed"));
    } else {
        qInfo() << "[MainWindow] Ping OK:" << result;
        if (result.value(QStringLiteral("pong")).toBool()) {
            m_statusLabel->setText(
                QStringLiteral("phpintel: ready %1")
                    .arg(result.value(QStringLiteral("version")).toString()));
        } else {
            m_statusLabel->setText(QStringLiteral("phpintel: invalid handshake response"));
            qWarning() << "[MainWindow] Unexpected phpintel handshake response:" << result;
        }
    }
}
