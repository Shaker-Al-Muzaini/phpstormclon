#include <gtest/gtest.h>

#include <QApplication>
#include <QCompleter>
#include <QFile>
#include <QJsonArray>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "forge/core/DiagnosticsModel.h"
#include "forge/core/Settings.h"
#include "GitPanel.h"
#include "PhpEditorController.h"

namespace {
void put(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write(bytes), bytes.size());
}
QByteArray git(const QString& directory, const QStringList& arguments) {
    QProcess process;
    process.setWorkingDirectory(directory);
    process.start("git", arguments);
    if (!process.waitForFinished(10000) || process.exitCode()) {
        ADD_FAILURE() << process.readAllStandardError().constData();
        return {};
    }
    return process.readAllStandardOutput();
}
QJsonObject edit(int start, int end, const QString& text) {
    return {{"range", QJsonObject{{"start", QJsonObject{{"line", 0}, {"character", start}}},
                                  {"end", QJsonObject{{"line", 0}, {"character", end}}}}},
            {"newText", text}};
}
}  // namespace
TEST(LanguageEdits, AtomicUtf16EditsValidateOverlapAndUndoTogether) {
    QPlainTextEdit editor;
    editor.setPlainText(QString::fromUtf8("عربي hello world"));
    EXPECT_FALSE(forge::editor::applyEdits(editor.document(), {edit(0, 6, "a"), edit(5, 8, "b")}));
    EXPECT_EQ(editor.toPlainText(), QString::fromUtf8("عربي hello world"));
    EXPECT_FALSE(forge::editor::applyEdits(editor.document(), {edit(30, 40, "bad")}));
    EXPECT_TRUE(
        forge::editor::applyEdits(editor.document(), {edit(5, 10, "good"), edit(11, 16, "code")}));
    EXPECT_EQ(editor.toPlainText(), QString::fromUtf8("عربي good code"));
    editor.undo();
    EXPECT_EQ(editor.toPlainText(), QString::fromUtf8("عربي hello world"));
}
TEST(GitPanel, StagesUnstagesCommitsAndDiffsArabicPathsInARealRepository) {
    if (QStandardPaths::findExecutable("git").isEmpty()) GTEST_SKIP() << "Git not installed";
    QTemporaryDir directory;
    git(directory.path(), {"init"});
    git(directory.path(), {"config", "user.name", "Forge Test"});
    git(directory.path(), {"config", "user.email", "test@example.invalid"});
    const auto file = QString::fromUtf8("ملف test.php");
    put(directory.filePath(file), "<?php echo 'before';\n");
    GitPanel panel;
    panel.show();
    panel.setRoot(directory.path());
    auto* files = panel.findChild<QTreeWidget*>("GitFiles");
    QTRY_COMPARE_WITH_TIMEOUT(files->topLevelItemCount(), 1, 5000);
    files->topLevelItem(0)->setSelected(true);
    QTRY_VERIFY_WITH_TIMEOUT(!panel.isBusy(), 5000);
    panel.findChild<QPushButton*>("GitStage")->click();
    QTRY_COMPARE_WITH_TIMEOUT(files->topLevelItem(0)->text(0), QString("A "), 5000);
    files->topLevelItem(0)->setSelected(true);
    QTRY_VERIFY_WITH_TIMEOUT(!panel.isBusy(), 5000);
    panel.findChild<QPushButton*>("GitUnstage")->click();
    QTRY_COMPARE_WITH_TIMEOUT(files->topLevelItem(0)->text(0), QString("??"), 5000);
    files->topLevelItem(0)->setSelected(true);
    QTRY_VERIFY_WITH_TIMEOUT(!panel.isBusy(), 5000);
    panel.findChild<QPushButton*>("GitStage")->click();
    QTRY_COMPARE_WITH_TIMEOUT(files->topLevelItem(0)->text(0), QString("A "), 5000);
    panel.findChild<QPlainTextEdit*>("GitCommitMessage")
        ->setPlainText(QString::fromUtf8("إضافة الملف"));
    panel.findChild<QPushButton*>("GitCommit")->click();
    QTRY_COMPARE_WITH_TIMEOUT(files->topLevelItemCount(), 0, 5000);
    EXPECT_EQ(QString::fromUtf8(git(directory.path(), {"log", "-1", "--format=%s"})).trimmed(),
              QString::fromUtf8("إضافة الملف"));
    put(directory.filePath(file), "<?php echo 'after';\n");
    panel.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(files->topLevelItemCount(), 1, 5000);
    files->topLevelItem(0)->setSelected(true);
    auto* diff = panel.findChild<QPlainTextEdit*>("GitDiff");
    QTRY_VERIFY_WITH_TIMEOUT(diff->toPlainText().contains("+<?php echo 'after';"), 5000);
    EXPECT_TRUE(diff->toPlainText().contains("-<?php echo 'before';"));
}
TEST(PhpEditor, RealCompletionInsertsOneUndoAndDefinitionOpensAnotherFile) {
    const auto script = qEnvironmentVariable("FORGE_TEST_LSP_SCRIPT");
    if (script.isEmpty()) {
        if (qEnvironmentVariableIsSet("FORGE_REQUIRE_LSP_TESTS")) FAIL() << "Missing PHP server";
        GTEST_SKIP() << "Set FORGE_TEST_LSP_SCRIPT";
    }
    QTemporaryDir directory;
    const auto definition = directory.filePath("Service.php");
    const auto usage = directory.filePath("use.php");
    put(definition, "<?php class Service { public function greet(): string { return 'hi'; } }\n");
    put(usage, "<?php $service = new Service(); $service->gr");
    forge::core::Settings settings;
    settings.setValue("php.language.script", script);
    settings.setValue("php.language.node", QStandardPaths::findExecutable("node"));
    QWidget window;
    auto* layout = new QVBoxLayout(&window);
    auto* editor = new QPlainTextEdit(&window);
    layout->addWidget(editor);
    editor->setProperty("forgeSourceEditor", true);
    editor->setProperty("filePath", usage);
    editor->setPlainText("<?php $service = new Service(); $service->gr");
    editor->moveCursor(QTextCursor::End);
    forge::core::DiagnosticsModel diagnostics;
    PhpEditorController controller(&window, &diagnostics);
    controller.attach(editor);
    controller.setWorkspace(directory.path());
    window.show();
    window.activateWindow();
    editor->setFocus();
    QTRY_VERIFY_WITH_TIMEOUT(controller.isReady(), 45000);
    auto* completer = controller.findChild<QCompleter*>("PhpCompleter");
    ASSERT_NE(completer, nullptr);
    bool found = false;
    for (int attempt = 0; attempt < 40 && !found; ++attempt) {
        controller.complete(editor);
        QTest::qWait(400);
        for (int row = 0; row < completer->model()->rowCount(); ++row) {
            const auto index = completer->model()->index(row, 0);
            if (index.data().toString().startsWith("greet")) {
                ASSERT_TRUE(
                    QMetaObject::invokeMethod(completer, "activated", Q_ARG(QModelIndex, index)));
                found = true;
                break;
            }
        }
    }
    ASSERT_TRUE(found);
    QTRY_VERIFY_WITH_TIMEOUT(editor->toPlainText().endsWith("->greet"), 5000);
    editor->undo();
    EXPECT_TRUE(editor->toPlainText().endsWith("->gr"));
    editor->setPlainText("<?php $service = new Service(); $service->greet();");
    auto cursor = editor->textCursor();
    cursor.setPosition(23);
    editor->setTextCursor(cursor);
    QString opened;
    controller.openLocation = [&opened](const QString& path, int, int) { opened = path; };
    controller.definition(editor);
    QTRY_COMPARE_WITH_TIMEOUT(opened, definition, 10000);
}

#include <QCheckBox>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTableWidget>
#include <QTextBlock>
#include <QToolButton>

#include "DatabasePanel.h"
#include "EditorWidgets.h"
#include "InspectionPanel.h"

TEST(InspectionReports, MapsRealPhpstanAndPhpcsJsonWithoutLosingColumnsOrSources) {
    QTemporaryDir root;
    const auto stan =
        QJsonDocument::fromJson(
            R"({"files":{"app/Test.php":{"messages":[{"message":"Call to an undefined method A::missing().","line":9,"identifier":"method.notFound"}]}},"errors":[]})")
            .object();
    const auto results = forge::tools::parseReport(stan, "PHPStan", root.path());
    ASSERT_EQ(results.value(root.filePath("app/Test.php")).size(), 1);
    EXPECT_EQ(results.value(root.filePath("app/Test.php")).first().line, 9);
    EXPECT_EQ(results.value(root.filePath("app/Test.php")).first().code, "method.notFound");
    const auto phpcs =
        QJsonDocument::fromJson(
            R"({"files":{"file.php":{"messages":[{"message":"Expected 4 spaces; found 2","type":"WARNING","line":3,"column":5,"source":"Generic.WhiteSpace"}]}}})")
            .object();
    const auto result = forge::tools::parseReport(phpcs, "PHP_CodeSniffer", root.path())
                            .value(root.filePath("file.php"))
                            .first();
    EXPECT_EQ(result.col, 5);
    EXPECT_EQ(result.severity, forge::core::DiagnosticSeverity::Warning);
}
TEST(EditorDiagnostics, PreviousArrowSelectsNearestEarlierProblemIncludingSameLine) {
    ForgeCodeEditor editor;
    editor.setPlainText(QString("line\n").repeated(30));
    QVector<forge::core::Diagnostic> diagnostics;
    for (int line : {20, 3, 10}) {
        forge::core::Diagnostic item;
        item.line = line;
        item.col = 2;
        item.endLine = line;
        item.endCol = 3;
        item.message = "problem";
        diagnostics.append(item);
    }
    editor.setDiagnostics(diagnostics);
    auto cursor = editor.textCursor();
    cursor.setPosition(editor.document()->findBlockByNumber(24).position());
    editor.setTextCursor(cursor);
    for (auto* button : editor.findChildren<QToolButton*>())
        if (button->toolTip() == "Previous problem") button->click();
    EXPECT_EQ(editor.textCursor().blockNumber(), 19);
    for (auto* button : editor.findChildren<QToolButton*>())
        if (button->toolTip() == "Previous problem") button->click();
    EXPECT_EQ(editor.textCursor().blockNumber(), 9);
}
TEST(DatabasePanel, RunsRealSqliteQueriesAndDisplaysNullAndArabicText) {
    auto binary = QCoreApplication::applicationDirPath() + "/dbtools";
#ifdef Q_OS_WIN
    binary += ".exe";
#endif
    if (!QFileInfo::exists(binary)) {
        if (qEnvironmentVariableIsSet("FORGE_REQUIRE_DB_TESTS")) FAIL() << "dbtools missing";
        GTEST_SKIP() << "Build dbtools next to tests";
    }
    DatabasePanel panel;
    panel.show();
    panel.findChild<QLineEdit*>("DatabaseUrl")->setText("sqlite::memory:");
    panel.findChild<QCheckBox*>()->setChecked(false);
    panel.findChild<QPushButton*>("DatabaseConnect")->click();
    auto* tables = panel.findChild<QListWidget*>();
    Q_UNUSED(tables);
    auto* sql = panel.findChild<QPlainTextEdit*>("DatabaseSql");
    auto* execute = panel.findChild<QPushButton*>("DatabaseExecute");
    auto* result = panel.findChild<QTableWidget*>("DatabaseResults");
    QTRY_VERIFY_WITH_TIMEOUT(
        panel.findChild<QLabel*>("DatabaseStatus")->text().contains("tables / views"), 5000);
    sql->setPlainText(QString::fromUtf8("SELECT 'مرحبا' AS greeting, NULL AS optional"));
    execute->click();
    QTRY_COMPARE_WITH_TIMEOUT(result->rowCount(), 1, 5000);
    ASSERT_NE(result->item(0, 0), nullptr);
    EXPECT_EQ(result->item(0, 0)->text(), QString::fromUtf8("مرحبا"));
    EXPECT_EQ(result->item(0, 1)->text(), "NULL");
}

TEST(InspectionTools, RealPhpstanAndPhpcsReportsReachProblemsAndRejectChangedBuffers) {
    const auto php = qEnvironmentVariable("FORGE_TEST_PHP");
    const auto stan = qEnvironmentVariable("FORGE_TEST_PHPSTAN");
    const auto phpcs = qEnvironmentVariable("FORGE_TEST_PHPCS");
    if (php.isEmpty() || stan.isEmpty() || phpcs.isEmpty())
        GTEST_SKIP() << "Set FORGE_TEST_PHP, FORGE_TEST_PHPSTAN and FORGE_TEST_PHPCS for external "
                        "tool integration";
    QTemporaryDir directory;
    const auto file = directory.filePath("example.php");
    put(file,
        "<?php\nclass CheckExample { public function run(): string { return 42; } }\n(new "
        "CheckExample())->missing();\n");
    forge::core::Settings settings;
    forge::core::DiagnosticsModel model;
    QWidget window;
    auto* layout = new QVBoxLayout(&window);
    auto* editor = new QPlainTextEdit(&window);
    editor->setProperty("forgeSourceEditor", true);
    editor->setProperty("filePath", file);
    editor->setPlainText("original");
    editor->document()->setModified(false);
    layout->addWidget(editor);
    auto* panel = new InspectionPanel(&model, &window);
    panel->setRoot(directory.path());
    layout->addWidget(panel);
    panel->findChild<QLineEdit*>("InspectionPhp")->setText(php);
    for (int tool = 0; tool < 2; ++tool) {
        panel->findChild<QComboBox*>("InspectionTool")->setCurrentIndex(tool);
        panel->findChild<QLineEdit*>("InspectionScript")->setText(tool == 0 ? stan : phpcs);
        panel->findChild<QPushButton*>("InspectionRun")->click();
        auto* output = panel->findChild<QPlainTextEdit*>("InspectionOutput");
        QTRY_VERIFY_WITH_TIMEOUT(output->toPlainText().contains("findings"), 30000);
        const auto source = tool == 0 ? QString("PHPStan") : QString("PHP_CodeSniffer");
        bool found = false;
        for (const auto& diagnostic : model.diagnosticsForFile(file))
            found |= diagnostic.source == source;
        EXPECT_TRUE(found) << output->toPlainText().toStdString();
    }
    model.clearSource("PHP_CodeSniffer");
    panel->findChild<QPushButton*>("InspectionRun")->click();
    editor->insertPlainText("edit while checking");
    auto* output = panel->findChild<QPlainTextEdit*>("InspectionOutput");
    QTRY_VERIFY_WITH_TIMEOUT(output->toPlainText().contains("1 changed files skipped"), 30000);
    for (const auto& diagnostic : model.diagnosticsForFile(file))
        EXPECT_NE(diagnostic.source, "PHP_CodeSniffer");
}
