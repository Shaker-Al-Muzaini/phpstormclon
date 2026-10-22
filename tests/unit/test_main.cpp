/**
 * Forge IDE C++ unit tests — entry point.
 * Individual test files are added here as milestones progress.
 *
 * M0: Smoke test only (verify the test harness works).
 */
#include <gtest/gtest.h>
#include <QString>

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
