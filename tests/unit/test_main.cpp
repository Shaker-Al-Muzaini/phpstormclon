/**
 * Forge IDE C++ unit tests — entry point.
 * Individual test files are added here as milestones progress.
 *
 * M0: Smoke test only (verify the test harness works).
 */
#include <gtest/gtest.h>
#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QString>

#include "forge/core/ActionManager.h"

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    Q_UNUSED(application);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

TEST(SmokeTest, QStringWorks) {
    // Verify Qt is linked correctly in the test binary
    const QString s = QStringLiteral("Forge IDE");
    EXPECT_EQ(s.toStdString(), "Forge IDE");
}

TEST(SmokeTest, CompileDefinitionsWork) {
    // Verify CMake compile definitions flow through
    const std::string name = FORGE_PRODUCT_NAME;
    EXPECT_FALSE(name.empty());
}

TEST(ActionManager, RegisteredActionRunsItsHandler) {
    forge::core::ActionManager actions;
    int callCount = 0;
    QAction* action = actions.registerAction(
        QStringLiteral("forge.test.action"),
        QStringLiteral("Test Action"),
        [&callCount]() { ++callCount; });

    ASSERT_NE(action, nullptr);
    action->trigger();
    EXPECT_EQ(callCount, 1);
}

TEST(ActionManager, AppliesKeymapToRegisteredAction) {
    forge::core::ActionManager actions;
    QAction* action = actions.registerAction(
        QStringLiteral("forge.test.action"),
        QStringLiteral("Test Action"),
        []() {});

    actions.applyKeymap({{QStringLiteral("forge.test.action"), QStringLiteral("Ctrl+S")}});

    ASSERT_NE(action, nullptr);
    EXPECT_EQ(action->shortcut().toString(), QStringLiteral("Ctrl+S"));
}

TEST(ActionManager, ApplicationShortcutWorksFromChildWidget) {
    QMainWindow window;
    QPlainTextEdit editor(&window);
    window.setCentralWidget(&editor);

    int callCount = 0;
    auto* action = new QAction(QStringLiteral("New File"), &window);
    action->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
    action->setShortcutContext(Qt::ApplicationShortcut);
    QObject::connect(action, &QAction::triggered, [&callCount]() { ++callCount; });
    window.addAction(action);
    window.show();
    window.activateWindow();
    editor.setFocus();
    QApplication::processEvents();

    QKeyEvent keyPress(QEvent::KeyPress, Qt::Key_N, Qt::ControlModifier,
                       QStringLiteral("n"));
    QApplication::sendEvent(&editor, &keyPress);

    EXPECT_EQ(callCount, 1);
    window.close();
}
