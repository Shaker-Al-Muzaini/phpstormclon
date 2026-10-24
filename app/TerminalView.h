#pragma once

#include <functional>
#include <memory>
#include <QPlainTextEdit>
#include <QStringList>
#include <QTextCursor>

class QCompleter;
class QStringListModel;

class TerminalOutputView final : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit TerminalOutputView(QWidget* parent = nullptr);
    ~TerminalOutputView() override;
    void setCommandHandler(std::function<void(const QString&)> handler);
    void setSharedHistory(const std::shared_ptr<QStringList>& history);
    void setWorkingDirectory(const QString& directory);
    QString workingDirectory() const {
        return m_workingDirectory;
    }
    QString currentInput() const;
    QString suggestedSuffix() const;
    QStringList completionCandidates() const {
        return m_candidates;
    }
    void showCompletions();
    void appendOutput(const QString& text);
    QString submitCurrentCommand();
    void clearTerminal();

signals:
    void commandSubmitted(const QString& command);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void insertFromMimeData(const QMimeData* source) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void navigateHistory(int direction);
    void replaceInput(const QString& text);
    void ensureInputCursor();
    QStringList baseCandidates(const QString& input) const;
    void displayCandidates(const QStringList& candidates);
    void updateInput();

    QTextCursor m_inputStart;
    std::shared_ptr<QStringList> m_history = std::make_shared<QStringList>();
    QString m_historyDraft;
    int m_historyIndex = -1;
    QString m_workingDirectory;
    QStringList m_commands;
    QStringList m_candidates;
    QCompleter* m_completer;
    QStringListModel* m_completionModel;
    QString m_lastInput;
    QString m_promptTail;
    int m_completionGeneration = 0;
    std::function<void(const QString&)> m_commandHandler;
};
