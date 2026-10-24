#pragma once
#include <QKeyEvent>
#include <QHelpEvent>
#include <QScrollBar>
#include <QToolTip>

#include "WorkspaceSupport.h"
#include "forge/core/DiagnosticsModel.h"

class ForgeCodeEditor;
class ForgeSyntaxHighlighter;
class DiagnosticMarkerBar;

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

class DiagnosticMarkerBar final : public QWidget {
public:
    explicit DiagnosticMarkerBar(ForgeCodeEditor* editor);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    ForgeCodeEditor* m_editor;
};

class ForgeCodeEditor final : public QPlainTextEdit {
public:
    explicit ForgeCodeEditor(QWidget* parent = nullptr)
        : QPlainTextEdit(parent), m_lineNumberArea(new LineNumberArea(this)) {
        m_markerBar = new DiagnosticMarkerBar(this);
        m_inspectionWidget = new QWidget(this);
        auto* inspectionLayout = new QHBoxLayout(m_inspectionWidget);
        inspectionLayout->setContentsMargins(4, 1, 4, 1);
        inspectionLayout->setSpacing(2);
        m_inspectionCount = new QLabel(m_inspectionWidget);
        m_previousDiagnostic = new QToolButton(m_inspectionWidget);
        m_previousDiagnostic->setText(QStringLiteral("↑"));
        m_previousDiagnostic->setToolTip(QStringLiteral("Previous problem"));
        m_nextDiagnostic = new QToolButton(m_inspectionWidget);
        m_nextDiagnostic->setText(QStringLiteral("↓"));
        m_nextDiagnostic->setToolTip(QStringLiteral("Next problem"));
        inspectionLayout->addWidget(m_inspectionCount);
        inspectionLayout->addWidget(m_previousDiagnostic);
        inspectionLayout->addWidget(m_nextDiagnostic);
        connect(m_previousDiagnostic, &QToolButton::clicked, this,
                [this]() { navigateDiagnostics(-1); });
        connect(m_nextDiagnostic, &QToolButton::clicked, this,
                [this]() { navigateDiagnostics(1); });
        m_inspectionWidget->adjustSize();
        m_inspectionWidget->hide();
        connect(this, &QPlainTextEdit::blockCountChanged, this,
                [this]() {
                    updateLineNumberAreaWidth();
                    m_markerBar->update();
                });
        connect(this, &QPlainTextEdit::updateRequest, this,
                [this](const QRect& rect, int dy) {
                    updateLineNumberArea(rect, dy);
                    m_markerBar->update();
                });
        connect(this, &QPlainTextEdit::cursorPositionChanged, this,
                [this]() { highlightCurrentLine(); });
        connect(verticalScrollBar(), &QScrollBar::rangeChanged, this,
                [this]() { updateDiagnosticOverlayGeometry(); });
        connect(verticalScrollBar(), &QScrollBar::valueChanged, m_markerBar,
                qOverload<>(&QWidget::update));
        connect(document(), &QTextDocument::contentsChanged, this,
                [this]() { refreshFunctionFolds(); });
        updateLineNumberAreaWidth();
        updateDiagnosticOverlayGeometry();
        highlightCurrentLine();
    }

    void setSyntaxHighlighter(QSyntaxHighlighter* highlighter) {
        m_syntaxHighlighter = highlighter;
    }

    QSyntaxHighlighter* syntaxHighlighter() const {
        return m_syntaxHighlighter;
    }

    void setDiagnostics(const QVector<forge::core::Diagnostic>& diagnostics) {
        m_diagnostics = diagnostics;
        updateDiagnosticSelections();
        updateDiagnosticOverlayGeometry();
        m_markerBar->update();
        int errors = 0;
        int warnings = 0;
        for (const auto& diagnostic : m_diagnostics) {
            errors += diagnostic.severity == forge::core::DiagnosticSeverity::Error;
            warnings += diagnostic.severity == forge::core::DiagnosticSeverity::Warning;
        }
        if (m_diagnostics.isEmpty()) {
            m_inspectionWidget->hide();
        } else {
            m_inspectionCount->setText(
                QStringLiteral("✖ %1  ⚠ %2").arg(errors).arg(warnings));
            m_inspectionCount->setToolTip(QStringLiteral("%1 errors, %2 warnings")
                                              .arg(errors)
                                              .arg(warnings));
            m_inspectionWidget->adjustSize();
            m_inspectionWidget->show();
            m_inspectionWidget->raise();
        }
    }

    void setDiagnosticColors(const QColor& error, const QColor& warning, const QColor& info) {
        m_errorColor = error;
        m_warningColor = warning;
        m_infoColor = info;
        updateDiagnosticSelections();
        m_markerBar->update();
    }

    void paintDiagnosticMarkers(QPaintEvent* event) const {
        QPainter painter(m_markerBar);
        painter.fillRect(event->rect(), palette().color(QPalette::Base));
        if (blockCount() <= 1) {
            return;
        }
        for (const auto& diagnostic : m_diagnostics) {
            const QColor color =
                diagnostic.severity == forge::core::DiagnosticSeverity::Error
                    ? m_errorColor
                    : diagnostic.severity == forge::core::DiagnosticSeverity::Warning
                          ? m_warningColor
                          : m_infoColor;
            const qreal y = (diagnostic.line - 1.0) / (blockCount() - 1) * m_markerBar->height();
            painter.fillRect(QRectF(0, y, m_markerBar->width(), 3), color);
        }
    }

    void setThemeFont(QFont font) {
        m_themeFontSize = qMax(8, font.pointSize());
        font.setPointSize(qBound(8, m_themeFontSize + m_zoomOffset, 36));
        QPlainTextEdit::setFont(font);
        setTabStopDistance(QFontMetrics(font).horizontalAdvance(QLatin1Char(' ')) * 4);
    }

    int lineNumberAreaWidth() const {
        const int digits = qMax(2, static_cast<int>(QString::number(qMax(1, blockCount())).size()));
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
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Tab && event->modifiers() == Qt::NoModifier && !isReadOnly() &&
            !textCursor().hasSelection()) {
            const int size = property("indentSize").isValid() ? property("indentSize").toInt() : 4;
            textCursor().insertText(QString(qBound(1, size, 8), QLatin1Char(' ')));
            event->accept();
            return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }
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
        updateDiagnosticOverlayGeometry();
    }

    bool event(QEvent* event) override {
        if (event->type() == QEvent::ToolTip && !m_diagnostics.isEmpty()) {
            auto* help = static_cast<QHelpEvent*>(event);
            const QTextCursor cursor = cursorForPosition(help->pos());
            const int line = cursor.blockNumber() + 1;
            const int column = cursor.positionInBlock() + 1;
            for (const auto& diagnostic : m_diagnostics) {
                const bool withinRange =
                    diagnostic.col == diagnostic.endCol
                        ? column == diagnostic.col || column + 1 == diagnostic.col
                        : column >= diagnostic.col &&
                              (line < diagnostic.endLine ||
                               (line == diagnostic.endLine && column <= diagnostic.endCol));
                if (line == diagnostic.line && withinRange) {
                    QToolTip::showText(help->globalPos(), diagnostic.message, this);
                    return true;
                }
            }
        }
        return QPlainTextEdit::event(event);
    }

private:
    void updateDiagnosticOverlayGeometry() {
        if (!m_markerBar || !m_inspectionWidget) {
            return;
        }
        m_markerBar->setGeometry(qMax(0, verticalScrollBar()->width() - 4), 0, 4,
                                 verticalScrollBar()->height());
        m_markerBar->setVisible(!m_diagnostics.isEmpty());
        m_markerBar->raise();
        const int right = verticalScrollBar()->isVisible() ? verticalScrollBar()->width() : 4;
        m_inspectionWidget->move(qMax(4, width() - m_inspectionWidget->width() - right - 10), 4);
        m_inspectionWidget->raise();
    }

    void updateDiagnosticSelections() {
        highlightCurrentLine();
    }

    void navigateDiagnostics(int direction) {
        if (m_diagnostics.isEmpty()) {
            return;
        }
        const int currentLine = textCursor().blockNumber() + 1;
        int selected = direction > 0 ? 0 : m_diagnostics.size() - 1;
        for (int index = 0; index < m_diagnostics.size(); ++index) {
            if ((direction > 0 && m_diagnostics.at(index).line > currentLine) ||
                (direction < 0 && m_diagnostics.at(index).line < currentLine)) {
                selected = index;
                break;
            }
        }
        const auto& diagnostic = m_diagnostics.at(selected);
        QTextBlock block = document()->findBlockByNumber(diagnostic.line - 1);
        if (!block.isValid()) {
            return;
        }
        QTextCursor cursor(block);
        cursor.setPosition(block.position() +
                           qBound(0, diagnostic.col - 1, block.text().size()));
        setTextCursor(cursor);
        centerCursor();
        setFocus();
    }

    QColor severityColor(forge::core::DiagnosticSeverity severity) const {
        return severity == forge::core::DiagnosticSeverity::Error
                   ? m_errorColor
                   : severity == forge::core::DiagnosticSeverity::Warning ? m_warningColor
                                                                           : m_infoColor;
    }

    void refreshFunctionFolds() {
        m_functionFolds.clear();
        m_collapsedFolds.clear();
        if (document()->characterCount() > 200000) return;
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
                    while (index + 1 < line.size() && (line.at(index + 1).isLetterOrNumber() ||
                                                       line.at(index + 1) == QLatin1Char('_'))) {
                        ++index;
                    }
                    if (line.mid(wordStart, index - wordStart + 1) == QStringLiteral("function")) {
                        awaitingFunctionBody = true;
                        awaitingFunctionBlockNumber = block.blockNumber();
                    }
                    continue;
                }
                if (current == QLatin1Char('{')) {
                    braces.append(
                        {awaitingFunctionBody ? awaitingFunctionBlockNumber : block.blockNumber(),
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
        for (const auto& diagnostic : m_diagnostics) {
            QTextBlock block = document()->findBlockByNumber(diagnostic.line - 1);
            if (!block.isValid()) {
                continue;
            }
            int start = block.position() + qBound(0, diagnostic.col - 1, block.text().size());
            QTextBlock endBlock = document()->findBlockByNumber(diagnostic.endLine - 1);
            if (!endBlock.isValid()) {
                endBlock = block;
            }
            int end = endBlock.position() + qBound(0, diagnostic.endCol - 1, endBlock.text().size());
            if (end <= start) {
                if (start > block.position()) {
                    --start;
                }
                end = qMin(block.position() + block.text().size(), start + 1);
            }
            QTextEdit::ExtraSelection selection;
            selection.cursor = QTextCursor(document());
            selection.cursor.setPosition(start);
            selection.cursor.setPosition(qMax(start, end), QTextCursor::KeepAnchor);
            selection.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            selection.format.setUnderlineColor(severityColor(diagnostic.severity));
            selection.format.setToolTip(diagnostic.message);
            selections.append(selection);
        }
        setExtraSelections(selections);
        m_lineNumberArea->update();
    }

    LineNumberArea* m_lineNumberArea;
    DiagnosticMarkerBar* m_markerBar = nullptr;
    QWidget* m_inspectionWidget = nullptr;
    QLabel* m_inspectionCount = nullptr;
    QToolButton* m_previousDiagnostic = nullptr;
    QToolButton* m_nextDiagnostic = nullptr;
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
    QColor m_errorColor = QColor(QStringLiteral("#E06C75"));
    QColor m_warningColor = QColor(QStringLiteral("#E5C07B"));
    QColor m_infoColor = QColor(QStringLiteral("#61AFEF"));
    QVector<forge::core::Diagnostic> m_diagnostics;
};

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
            setFormat(static_cast<int>(item.capturedStart()),
                      static_cast<int>(item.capturedLength()), format);
        }
    }

    void highlightBlockComment(const QString& text) {
        qsizetype start = previousBlockState() == 1 ? 0 : text.indexOf(QStringLiteral("/*"));
        while (start >= 0) {
            const qsizetype end = text.indexOf(QStringLiteral("*/"), start);
            if (end < 0) {
                setCurrentBlockState(1);
                const int remaining = static_cast<int>(text.size() - start);
                setFormat(static_cast<int>(start), remaining, m_comment);
                return;
            }
            const qsizetype length = end - start + 2;
            setFormat(static_cast<int>(start), static_cast<int>(length), m_comment);
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
