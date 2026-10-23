#pragma once
#include "WorkspaceIncludes.h"

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
