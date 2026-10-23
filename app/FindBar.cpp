#include "FindBar.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QTextCursor>
#include <QVBoxLayout>
FindBar::FindBar(QWidget* parent) : QWidget(parent) {
    setObjectName("FindBar");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    auto* row = new QHBoxLayout;
    m_query = new QLineEdit(this);
    m_query->setPlaceholderText("Find in current file");
    m_query->setObjectName("FindQuery");
    row->addWidget(m_query, 1);
    m_case = new QCheckBox("Aa", this);
    m_case->setToolTip("Match case");
    m_word = new QCheckBox("Word", this);
    m_regex = new QCheckBox(".*", this);
    m_regex->setToolTip("Regular expression");
    for (auto* checkbox : {m_case, m_word, m_regex}) row->addWidget(checkbox);
    auto* previous = new QPushButton("↑", this);
    auto* next = new QPushButton("↓", this);
    auto* close = new QPushButton("×", this);
    for (auto* button : {previous, next, close}) {
        button->setFixedWidth(28);
        row->addWidget(button);
    }
    m_status = new QLabel(this);
    row->addWidget(m_status);
    layout->addLayout(row);
    m_replaceRow = new QWidget(this);
    auto* replacementRow = new QHBoxLayout(m_replaceRow);
    replacementRow->setContentsMargins(0, 0, 0, 0);
    m_replacement = new QLineEdit(this);
    m_replacement->setPlaceholderText("Replace with (literal text)");
    replacementRow->addWidget(m_replacement, 1);
    auto* replaceOne = new QPushButton("Replace", this);
    auto* replaceAll = new QPushButton("Replace All", this);
    replacementRow->addWidget(replaceOne);
    replacementRow->addWidget(replaceAll);
    layout->addWidget(m_replaceRow);
    connect(previous, &QPushButton::clicked, this, [this]() { find(true); });
    connect(next, &QPushButton::clicked, this, [this]() { find(); });
    connect(m_query, &QLineEdit::returnPressed, this, [this]() { find(); });
    connect(replaceOne, &QPushButton::clicked, this, [this]() { replace(false); });
    connect(replaceAll, &QPushButton::clicked, this, [this]() { replace(true); });
    connect(close, &QPushButton::clicked, this, [this]() {
        hide();
        if (m_editor) m_editor->setFocus();
    });
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, close, &QPushButton::click);
    hide();
}
void FindBar::setEditor(QPlainTextEdit* editor) {
    m_editor = editor;
    if (!editor) hide();
}
void FindBar::open(QPlainTextEdit* editor, bool replacement) {
    setEditor(editor);
    if (!editor) return;
    if (editor->textCursor().hasSelection() &&
        !editor->textCursor().selectedText().contains(QChar::ParagraphSeparator))
        m_query->setText(editor->textCursor().selectedText());
    m_replaceRow->setVisible(replacement);
    m_status->clear();
    show();
    m_query->setFocus();
    m_query->selectAll();
}
void FindBar::find(bool backwards) {
    if (!m_editor || m_query->text().isEmpty()) return;
    QString pattern =
        m_regex->isChecked() ? m_query->text() : QRegularExpression::escape(m_query->text());
    if (m_word->isChecked()) pattern = "\\b(?:" + pattern + ")\\b";
    QRegularExpression expression(pattern, m_case->isChecked()
                                               ? QRegularExpression::NoPatternOption
                                               : QRegularExpression::CaseInsensitiveOption);
    if (!expression.isValid()) {
        m_status->setText("Invalid regex");
        return;
    }
    QTextDocument::FindFlags flags;
    if (backwards) flags |= QTextDocument::FindBackward;
    if (m_editor->find(expression, flags)) {
        m_status->clear();
        return;
    }
    const auto saved = m_editor->textCursor();
    m_editor->moveCursor(backwards ? QTextCursor::End : QTextCursor::Start);
    if (m_editor->find(expression, flags))
        m_status->setText("Wrapped");
    else {
        m_editor->setTextCursor(saved);
        m_status->setText("No matches");
    }
}
void FindBar::replace(bool all) {
    if (!m_editor || m_editor->isReadOnly() || m_query->text().isEmpty()) return;
    QString pattern =
        m_regex->isChecked() ? m_query->text() : QRegularExpression::escape(m_query->text());
    if (m_word->isChecked()) pattern = "\\b(?:" + pattern + ")\\b";
    QRegularExpression expression(pattern, m_case->isChecked()
                                               ? QRegularExpression::NoPatternOption
                                               : QRegularExpression::CaseInsensitiveOption);
    if (!expression.isValid()) {
        m_status->setText("Invalid regex");
        return;
    }
    if (!all) {
        auto cursor = m_editor->textCursor();
        const auto match = expression.match(cursor.selectedText());
        if (cursor.hasSelection() && match.hasMatch() && match.capturedStart() == 0 &&
            match.capturedLength() == cursor.selectedText().size())
            cursor.insertText(m_replacement->text());
        find();
        return;
    }
    const auto text = m_editor->toPlainText();
    auto matches = expression.globalMatch(text);
    QList<QPair<int, int>> positions;
    while (matches.hasNext()) {
        const auto match = matches.next();
        positions.append(
            {static_cast<int>(match.capturedStart()), static_cast<int>(match.capturedLength())});
    }
    QTextCursor cursor(m_editor->document());
    cursor.beginEditBlock();
    for (auto it = positions.crbegin(); it != positions.crend(); ++it) {
        cursor.setPosition(it->first);
        cursor.setPosition(it->first + it->second, QTextCursor::KeepAnchor);
        cursor.insertText(m_replacement->text());
    }
    cursor.endEditBlock();
    m_status->setText(QStringLiteral("%1 replaced").arg(positions.size()));
}
