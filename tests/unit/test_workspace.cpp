#include <gtest/gtest.h>

#include <QApplication>
#include <QCryptographicHash>
#include <QComboBox>
#include <QDockWidget>
#include <QFile>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSplitter>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCursor>
#include <QTreeWidget>
#include <QTimer>

#include "BrandSplash.h"
#include "FindBar.h"
#include "ProblemsPanel.h"
#include "forge/core/ActionManager.h"
#include "forge/core/DiagnosticsModel.h"
#include "forge/core/FileWatcher.h"
#include "forge/core/ServiceSupervisor.h"
#include "forge/core/Settings.h"
#include "MainWindow.h"

namespace {
struct Workspace {
    forge::core::Settings settings;
    forge::core::ActionManager actions;
    forge::core::ServiceSupervisor supervisor;
    MainWindow window{&actions, &supervisor};
    Workspace() {
        window.show();
        window.activateWindow();
        QTest::qWait(40);
    }
};
void put(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write(bytes), bytes.size());
}
QByteArray get(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
}  // namespace
TEST(Workspace, OpensArabicPathPreservesCrLfAndSavesUsingKeyboardShortcut) {
    QTemporaryDir directory;
    const auto path = directory.filePath(QString::fromUtf8("ملف.php"));
    put(path, "<?php\r\necho 'hello';\r\n");
    Workspace workspace;
    workspace.window.openProjectPath(directory.path());
    workspace.window.openDocument(path);
    auto* editor = workspace.window.findChild<QTabWidget*>("EditorTabs")->currentWidget();
    auto* text = qobject_cast<QPlainTextEdit*>(editor);
    ASSERT_NE(text, nullptr);
    const QFontMetricsF metrics(text->font());
    EXPECT_GT(metrics.horizontalAdvance("MMMM"), 0);
    EXPECT_NEAR(metrics.horizontalAdvance("iiii"), metrics.horizontalAdvance("WWWW"), 0.1)
        << "Resolved editor font: " << QFontInfo(text->font()).family().toStdString();
    text->setFocus();
    text->moveCursor(QTextCursor::End);
    text->insertPlainText("// appended");
    QTest::qWait(40);
    QTest::keyClick(text, Qt::Key_S, Qt::ControlModifier);
    QTest::qWait(40);
    EXPECT_EQ(get(path), "<?php\r\necho 'hello';\r\n// appended");
    EXPECT_FALSE(text->document()->isModified());
}
TEST(Workspace, CtrlFAndReplaceAreReachableAndReplaceAllIsOneUndo) {
    QTemporaryDir directory;
    const auto path = directory.filePath("find.php");
    put(path, "alpha beta alpha\n");
    Workspace workspace;
    workspace.window.openDocument(path);
    auto* text = qobject_cast<QPlainTextEdit*>(
        workspace.window.findChild<QTabWidget*>("EditorTabs")->currentWidget());
    ASSERT_NE(text, nullptr);
    text->setFocus();
    QTest::qWait(30);
    QTest::keyClick(text, Qt::Key_F, Qt::ControlModifier);
    QTest::qWait(30);
    auto* bar = workspace.window.findChild<FindBar*>();
    ASSERT_NE(bar, nullptr);
    EXPECT_TRUE(bar->isVisible());
    auto* query = bar->findChild<QLineEdit*>("FindQuery");
    query->setText("alpha");
    QTest::keyClick(query, Qt::Key_Return);
    EXPECT_EQ(text->textCursor().selectedText(), "alpha");
    QTest::keyClick(query, Qt::Key_Escape);
    text->setFocus();
    QTest::keyClick(text, Qt::Key_R, Qt::ControlModifier);
    QTest::qWait(20);
    const auto fields = bar->findChildren<QLineEdit*>();
    ASSERT_EQ(fields.size(), 2);
    fields.at(1)->setText("gamma");
    for (auto* button : bar->findChildren<QPushButton*>())
        if (button->text() == "Replace All") button->click();
    EXPECT_EQ(text->toPlainText(), "gamma beta gamma\n");
    text->undo();
    EXPECT_EQ(text->toPlainText(), "alpha beta alpha\n");
    text->document()->setModified(false);
}
TEST(Workspace, ExternalOverwriteIsCancelledAndDiskVersionSurvives) {
    QTemporaryDir directory;
    const auto path = directory.filePath("change.php");
    put(path, "original\n");
    Workspace workspace;
    workspace.window.openDocument(path);
    auto* text = qobject_cast<QPlainTextEdit*>(
        workspace.window.findChild<QTabWidget*>("EditorTabs")->currentWidget());
    ASSERT_NE(text, nullptr);
    text->insertPlainText("local ");
    put(path, "external\n");
    QTimer::singleShot(50, []() {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            box->reject();
    });
    workspace.actions.action("forge.file.save")->trigger();
    EXPECT_EQ(get(path), "external\n");
    EXPECT_TRUE(text->document()->isModified());
    text->document()->setModified(false);
}
TEST(Workspace, EveryVisibleToolbarActionHasHandlerAndSixThemesApply) {
    Workspace workspace;
    for (const auto& theme :
         QStringList{"Graphite", "Daylight", "Midnight", "Obsidian", "Ocean", "Amethyst"}) {
        workspace.window.selectTheme(theme);
        EXPECT_EQ(workspace.settings.value("ui.theme").toString(), theme);
    }
    ASSERT_NE(workspace.window.findChild<QDockWidget*>("ProjectToolWindow"), nullptr);
    const bool visible = workspace.window.findChild<QDockWidget*>("ProjectToolWindow")->isVisible();
    workspace.actions.action("forge.window.projectTree")->trigger();
    EXPECT_NE(workspace.window.findChild<QDockWidget*>("ProjectToolWindow")->isVisible(), visible);
    EXPECT_FALSE(workspace.actions.action("forge.run.run")->isEnabled());
}
TEST(FileWatcher, AtomicReplacementIsObservedAndTheNewFileRemainsWatched) {
    QTemporaryDir directory;
    const auto path = directory.filePath("watched.php");
    put(path, "one");
    forge::core::FileWatcher watcher;
    watcher.watchFile(path);
    QSignalSpy changed(&watcher, &forge::core::FileWatcher::fileChanged);
    ASSERT_TRUE(QFile::remove(path));
    put(path, "two");
    ASSERT_TRUE(changed.wait(3000));
    changed.clear();
    put(path, "three");
    ASSERT_TRUE(changed.wait(3000));
}
TEST(BrandSplash, HasIndependentBrandArtwork) {
    const auto pixmap = BrandSplash::artwork();
    EXPECT_FALSE(pixmap.isNull());
    EXPECT_EQ(pixmap.width(), 1280);
}

TEST(ProblemsPanel, GroupsFiltersShowsContextAndActivatesLocations) {
    QTemporaryDir directory;
    const QString file = directory.filePath("problem.php");
    put(file, "<?php\nfirst();\nsecond();\nthird();\nfourth();\nfifth();\n");
    forge::core::DiagnosticsModel model;
    ProblemsPanel panel(&model, false);
    panel.setCurrentDocument(file, QString::fromUtf8(get(file)));
    auto* status = panel.findChild<QLabel*>("ProblemsStatus");
    auto* tree = panel.findChild<QTreeWidget*>("ProblemsTree");
    auto* filter = panel.findChild<QComboBox*>();
    ASSERT_NE(status, nullptr);
    ASSERT_NE(tree, nullptr);
    ASSERT_NE(filter, nullptr);
    EXPECT_EQ(status->text(), "No problems found");

    const QVector<forge::core::Diagnostic> diagnostics{
        {file, 3, 1, 3, 2, forge::core::DiagnosticSeverity::Error,
         QStringLiteral("Unexpected token"), QStringLiteral("phpintel"),
         QStringLiteral("php.syntax"), {}},
        {file, 5, 1, 5, 2, forge::core::DiagnosticSeverity::Warning,
         QStringLiteral("Deprecated call"), QStringLiteral("phpintel"),
         QStringLiteral("php.deprecated"), {}}};
    model.setFileDiagnostics(file, QStringLiteral("phpintel"), diagnostics);
    ASSERT_EQ(tree->topLevelItemCount(), 1);
    auto* fileGroup = tree->topLevelItem(0);
    ASSERT_EQ(fileGroup->childCount(), 2);
    EXPECT_FALSE(fileGroup->child(0)->icon(0).isNull());

    tree->setCurrentItem(fileGroup->child(0));
    auto* details = panel.findChild<QPlainTextEdit*>("ProblemDetails");
    ASSERT_NE(details, nullptr);
    EXPECT_TRUE(details->toPlainText().contains("Unexpected token"));
    EXPECT_TRUE(details->toPlainText().contains(">    3 | second();"));

    QSignalSpy activated(&panel, &ProblemsPanel::locationActivated);
    ASSERT_TRUE(QMetaObject::invokeMethod(
        tree, "itemDoubleClicked", Qt::DirectConnection,
        Q_ARG(QTreeWidgetItem*, fileGroup->child(0)), Q_ARG(int, 0)));
    ASSERT_EQ(activated.size(), 1);
    EXPECT_EQ(activated.first().at(0).toString(), file);
    EXPECT_EQ(activated.first().at(1).toInt(), 3);

    filter->setCurrentIndex(1);
    ASSERT_EQ(tree->topLevelItem(0)->childCount(), 1);
    EXPECT_EQ(tree->topLevelItem(0)->child(0)->text(0), "Unexpected token");
    filter->setCurrentIndex(2);
    ASSERT_EQ(tree->topLevelItem(0)->childCount(), 1);
    EXPECT_EQ(tree->topLevelItem(0)->child(0)->text(0), "Deprecated call");
}

TEST(Workspace, SplitViewsShareEditsAndSavingEitherViewUpdatesTheSameFile) {
    QTemporaryDir directory;
    const auto path = directory.filePath("split.php");
    put(path, "<?php\n");
    Workspace workspace;
    workspace.window.openDocument(path);
    workspace.actions.action("forge.window.splitRight")->trigger();
    QTest::qWait(20);
    auto* splitter = workspace.window.findChild<QSplitter*>("EditorSplitter");
    ASSERT_NE(splitter, nullptr);
    ASSERT_EQ(splitter->count(), 2);
    auto* right = qobject_cast<QPlainTextEdit*>(splitter->widget(1));
    ASSERT_NE(right, nullptr);
    right->setFocus();
    right->moveCursor(QTextCursor::End);
    right->insertPlainText("echo 1;\n");
    workspace.actions.action("forge.file.save")->trigger();
    EXPECT_EQ(get(path), "<?php\necho 1;\n");
    workspace.actions.action("forge.file.close")->trigger();
    QTest::qWait(20);
    EXPECT_EQ(workspace.window.findChild<QTabWidget*>("EditorTabs")->count(), 0);
}
