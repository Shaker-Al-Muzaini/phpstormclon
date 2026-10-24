#include <gtest/gtest.h>

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QCompleter>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "TerminalView.h"

TEST(Terminal, HistoryGhostIsAcceptedWithoutExecutingTheCommand) {
    TerminalOutputView terminal;
    terminal.resize(700, 240);
    terminal.show();
    auto history = std::make_shared<QStringList>();
    history->append("git commit -m \"feature\"");
    terminal.setSharedHistory(history);
    terminal.appendOutput("PS project> ");
    QSignalSpy submitted(&terminal, &TerminalOutputView::commandSubmitted);
    QTest::keyClicks(&terminal, "git c");
    EXPECT_EQ(terminal.currentInput(), "git c");
    EXPECT_EQ(terminal.suggestedSuffix(), "ommit -m \"feature\"");
    QTest::keyClick(&terminal, Qt::Key_Right);
    EXPECT_EQ(terminal.currentInput(), "git commit -m \"feature\"");
    EXPECT_EQ(submitted.size(), 0);
    QTest::keyClick(&terminal, Qt::Key_Return);
    ASSERT_EQ(submitted.size(), 1);
    EXPECT_EQ(submitted.first().first().toString(), "git commit -m \"feature\"");
    EXPECT_TRUE(terminal.currentInput().isEmpty());
}

TEST(Terminal, AsynchronousOutputPreservesTheUnsubmittedDraft) {
    TerminalOutputView terminal;
    terminal.appendOutput("PS> ");
    terminal.insertPlainText("php artisan ser");
    terminal.appendOutput("\nbackground output\nPS> ");
    EXPECT_EQ(terminal.currentInput(), "php artisan ser");
    EXPECT_TRUE(terminal.toPlainText().endsWith("PS> php artisan ser"));
    EXPECT_EQ(terminal.submitCurrentCommand(), "php artisan ser");
    EXPECT_TRUE(terminal.currentInput().isEmpty());
}

TEST(Terminal, CompletesQuotedArabicDirectoryAndEnterOnlyAcceptsPopup) {
    QTemporaryDir directory;
    ASSERT_TRUE(QDir(directory.path()).mkdir(QString::fromUtf8("عربي folder")));
    TerminalOutputView terminal;
    terminal.resize(700, 240);
    terminal.show();
    terminal.setWorkingDirectory(directory.path());
    terminal.appendOutput("PS> ");
    terminal.insertPlainText("cd ");
    QSignalSpy submitted(&terminal, &TerminalOutputView::commandSubmitted);
    QTest::keyClick(&terminal, Qt::Key_Tab);
    QTRY_VERIFY_WITH_TIMEOUT(!terminal.completionCandidates().isEmpty(), 3000);
    bool directoryFound = false;
    for (const auto& candidate : terminal.completionCandidates())
        directoryFound |= candidate.contains(QString::fromUtf8("عربي folder"));
    EXPECT_TRUE(directoryFound);
    auto* completer = terminal.findChild<QCompleter*>("TerminalCompleter");
    ASSERT_NE(completer, nullptr);
    ASSERT_TRUE(completer->popup()->isVisible());
    QTest::keyClick(&terminal, Qt::Key_Return);
    EXPECT_EQ(submitted.size(), 0);
    EXPECT_TRUE(terminal.currentInput().contains(QString::fromUtf8("عربي folder")));
}

TEST(Terminal, CopyingTranscriptKeepsTheSelectionAndInputIntact) {
    TerminalOutputView terminal;
    terminal.appendOutput("server output\nPS> ");
    terminal.insertPlainText("git status");
    auto cursor = terminal.textCursor();
    cursor.setPosition(0);
    cursor.setPosition(13, QTextCursor::KeepAnchor);
    terminal.setTextCursor(cursor);
    QTest::keyClick(&terminal, Qt::Key_C, Qt::ControlModifier);
    EXPECT_EQ(QApplication::clipboard()->text(), "server output");
    EXPECT_EQ(terminal.currentInput(), "git status");
}
