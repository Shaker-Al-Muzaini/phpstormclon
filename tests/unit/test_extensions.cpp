#include <gtest/gtest.h>
#include <zlib.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtEndian>

#include "forge/core/ExtensionManager.h"
#include "forge/core/DiagnosticsModel.h"
#include "forge/core/PackageArchive.h"
#include "forge/core/ProjectSearch.h"
#include "forge/core/ThemeCatalog.h"

namespace {
void writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write(bytes), bytes.size());
}
QByteArray zip(const QByteArray& name, const QByteArray& body) {
    QByteArray local, central, end;
    auto put16 = [](QByteArray& out, quint16 value) {
        char b[2];
        qToLittleEndian(value, b);
        out.append(b, 2);
    };
    auto put32 = [](QByteArray& out, quint32 value) {
        char b[4];
        qToLittleEndian(value, b);
        out.append(b, 4);
    };
    const auto crc = static_cast<quint32>(
        crc32(0, reinterpret_cast<const Bytef*>(body.constData()), static_cast<uInt>(body.size())));
    put32(local, 0x04034b50);
    put16(local, 20);
    for (int i = 0; i < 4; ++i) put16(local, 0);
    put32(local, crc);
    put32(local, static_cast<quint32>(body.size()));
    put32(local, static_cast<quint32>(body.size()));
    put16(local, static_cast<quint16>(name.size()));
    put16(local, 0);
    local += name;
    local += body;
    put32(central, 0x02014b50);
    put16(central, 20);
    put16(central, 20);
    for (int i = 0; i < 4; ++i) put16(central, 0);
    put32(central, crc);
    put32(central, static_cast<quint32>(body.size()));
    put32(central, static_cast<quint32>(body.size()));
    put16(central, static_cast<quint16>(name.size()));
    for (int i = 0; i < 4; ++i) put16(central, 0);
    put32(central, 0);
    put32(central, 0);
    central += name;
    put32(end, 0x06054b50);
    put16(end, 0);
    put16(end, 0);
    put16(end, 1);
    put16(end, 1);
    put32(end, static_cast<quint32>(central.size()));
    put32(end, static_cast<quint32>(local.size()));
    put16(end, 0);
    return local + central + end;
}
}  // namespace
TEST(ThemeCatalog, JsonCommentsPreserveStringsAndAllowTrailingCommas) {
    const QByteArray source = R"({"url":"https://example.test/a/*b*/",// note)"
                              "\n"
                              R"("values":["a,}",],/* note */})";
    const auto bytes = forge::core::ThemeCatalog::stripJsonComments(source);
    const auto object = QJsonDocument::fromJson(bytes).object();
    EXPECT_EQ(object.value("url").toString(), "https://example.test/a/*b*/");
    EXPECT_EQ(object.value("values").toArray().size(), 1);
}
TEST(ThemeCatalog, SixThemesResolveAndExternalSyntaxColorsImport) {
    const auto themes = forge::core::ThemeCatalog::themes();
    EXPECT_GE(themes.size(), 6);
    for (const auto& name :
         QStringList{"Graphite", "Daylight", "Midnight", "Obsidian", "Ocean", "Amethyst"}) {
        const auto theme = forge::core::ThemeCatalog::load(name);
        EXPECT_EQ(theme.value("name").toString(), name);
        EXPECT_FALSE(theme.value("editor").toObject().isEmpty());
    }
    const auto converted = forge::core::ThemeCatalog::convertVsCode(
        QJsonDocument::fromJson(
            R"({"name":"Example","colors":{"editor.background":"#101010"},"tokenColors":[{"scope":"keyword.control.php","settings":{"foreground":"#ffcc00"}}]})")
            .object());
    EXPECT_EQ(converted.value("editor").toObject().value("background").toString(), "#101010");
    EXPECT_EQ(converted.value("syntax").toObject().value("keyword").toString(), "#ffcc00");
}
TEST(PackageArchive, StoredEntryExtractsAndTraversalAndBadChecksumFail) {
    QTemporaryDir directory;
    QString error;
    ASSERT_TRUE(forge::core::PackageArchive::extract(zip("extension/package.json", "{}"),
                                                     directory.path(), &error))
        << error.toStdString();
    QFile file(directory.filePath("extension/package.json"));
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(file.readAll(), "{}");
    EXPECT_FALSE(forge::core::PackageArchive::extract(zip("../escape.txt", "bad"), directory.path(),
                                                      &error));
    auto damaged = zip("x.txt", "ok");
    damaged[35] = 'X';
    EXPECT_FALSE(forge::core::PackageArchive::extract(damaged, directory.path(), &error));
}
TEST(Extensions, InstallDisableEnableUninstallAndRejectExecutableOrEscapingPackages) {
    QTemporaryDir source, installed;
    forge::core::ExtensionManager manager(installed.path());
    QString error;
    writeFile(
        source.filePath("package.json"),
        R"({"publisher":"test","name":"theme","displayName":"Test Theme","version":"1.0.0","license":"MIT","contributes":{"themes":[{"label":"Test Theme","path":"colors.json"}],"snippets":[{"language":"php","path":"snippets.json"}]}})");
    writeFile(source.filePath("colors.json"), R"({"colors":{"editor.background":"#0f1020"}})");
    writeFile(source.filePath("snippets.json"),
              R"({"Hello":{"prefix":"hello","body":["<?php","echo 'hello';"]}})");
    ASSERT_TRUE(manager.installFolder(source.path(), &error)) << error.toStdString();
    ASSERT_EQ(manager.installed().size(), 1);
    EXPECT_EQ(manager.snippets().size(), 1);
    ASSERT_TRUE(manager.setEnabled("test.theme", false, &error));
    EXPECT_EQ(manager.snippets().size(), 0);
    ASSERT_TRUE(manager.setEnabled("test.theme", true, &error));
    EXPECT_EQ(manager.snippets().size(), 1);
    writeFile(
        source.filePath("package.json"),
        R"({"publisher":"test","name":"theme","version":"2.0.0","main":"extension.js","contributes":{"themes":[{"path":"colors.json"}]}})");
    EXPECT_FALSE(manager.installFolder(source.path(), &error));
    EXPECT_EQ(manager.installed().at(0).toObject().value("version").toString(), "1.0.0");
    writeFile(
        source.filePath("package.json"),
        R"({"publisher":"test","name":"escape","version":"1.0.0","contributes":{"themes":[{"path":"../outside.json"}]}})");
    EXPECT_FALSE(manager.installFolder(source.path(), &error));
    ASSERT_TRUE(manager.uninstall("test.theme", &error));
    EXPECT_TRUE(manager.installed().isEmpty());
    EXPECT_FALSE(manager.uninstall("../outside", &error));
}
TEST(ProjectSearch, IndexesInBackgroundExcludesVendorAndFindsArabic) {
    QTemporaryDir directory;
    QDir().mkpath(directory.filePath("vendor"));
    writeFile(directory.filePath("a.php"),
              QString::fromUtf8("<?php\n// مرحبا\necho 'test';\n").toUtf8());
    writeFile(directory.filePath("vendor/skip.php"), "test");
    forge::core::ProjectSearch search;
    QSignalSpy indexed(&search, &forge::core::ProjectSearch::filesReady);
    search.setRoot(directory.path());
    ASSERT_TRUE(indexed.wait(5000));
    ASSERT_EQ(search.files().size(), 1);
    QVector<forge::core::SearchHit> hits;
    QObject::connect(&search, &forge::core::ProjectSearch::searchFinished, &search,
                     [&hits](const auto& result, bool) { hits = result; });
    QSignalSpy found(&search, &forge::core::ProjectSearch::searchFinished);
    search.findText(QString::fromUtf8("مرحبا"), true, false);
    ASSERT_TRUE(found.wait(5000));
    ASSERT_EQ(hits.size(), 1);
    EXPECT_EQ(hits.first().line, 2);
}

TEST(DiagnosticsModel, ReplacesOneSourcePerFileAndEmitsPerFileChanges) {
    QTemporaryDir directory;
    forge::core::DiagnosticsModel model;
    QSignalSpy fileChanged(&model, &forge::core::DiagnosticsModel::fileDiagnosticsChanged);
    const QString path = directory.filePath("src.php");
    forge::core::Diagnostic phpError{
        path, 4, 1, 4, 2, forge::core::DiagnosticSeverity::Error,
        QStringLiteral("unexpected token"), QStringLiteral("php -l"),
        QStringLiteral("parse.syntax"), {}};
    model.setFileDiagnostics(path, QStringLiteral("php -l"), {phpError});
    forge::core::Diagnostic languageError = phpError;
    languageError.source = QStringLiteral("phpintel");
    model.setFileDiagnostics(path, QStringLiteral("phpintel"), {languageError});
    EXPECT_EQ(model.diagnosticsForFile(path).size(), 2);
    model.clearFileSource(path, QStringLiteral("php -l"));
    ASSERT_EQ(model.diagnosticsForFile(path).size(), 1);
    EXPECT_EQ(model.diagnosticsForFile(path).first().source, QStringLiteral("phpintel"));
    EXPECT_EQ(fileChanged.size(), 3);
}

TEST(DiagnosticsModel, ParsesPhpLintErrorFormatsAndClearsOnSuccess) {
    const QString file = QStringLiteral("C:/project/app.php");
    auto diagnostics = forge::core::DiagnosticsModel::parsePhpLintOutput(
        file, QStringLiteral("Parse error: syntax error, unexpected token \"use\" in "
                             "C:/project/app.php on line 7\nErrors parsing C:/project/app.php"),
        255);
    ASSERT_EQ(diagnostics.size(), 1);
    EXPECT_EQ(diagnostics.first().file, QDir::cleanPath(QFileInfo(file).absoluteFilePath()));
    EXPECT_EQ(diagnostics.first().line, 7);
    EXPECT_EQ(diagnostics.first().source, QStringLiteral("php -l"));
    EXPECT_EQ(diagnostics.first().severity, forge::core::DiagnosticSeverity::Error);

    diagnostics = forge::core::DiagnosticsModel::parsePhpLintOutput(
        file, QStringLiteral("PHP Fatal error: Uncaught Error: failure in "
                             "C:/project/app.php on line 12"),
        255);
    ASSERT_EQ(diagnostics.size(), 1);
    EXPECT_EQ(diagnostics.first().line, 12);
    EXPECT_EQ(diagnostics.first().code, QStringLiteral("fatal.syntax"));
    EXPECT_TRUE(forge::core::DiagnosticsModel::parsePhpLintOutput(
                    file, QStringLiteral("No syntax errors detected"), 0)
                    .isEmpty());
    EXPECT_EQ(forge::core::DiagnosticsModel::parsePhpLintOutput(file, {}, 1).size(), 1);
}

TEST(DiagnosticsModel, ParsesLatestLaravelExceptionAndKeepsProjectFrames) {
    QTemporaryDir directory;
    const QString source = directory.filePath("app/Exceptions/Handler.php");
    const QString frame = directory.filePath("app/Services/Example.php");
    const QString log = directory.filePath("storage/logs/laravel.log");
    const QByteArray contents =
        QStringLiteral("[2026-10-24 00:00:00] local.ERROR: OldException: old at "
                       "%1:2\n"
                       "[2026-10-24 00:01:00] local.ERROR: "
                       "App\\Exceptions\\FailureException: Runtime failure at %2:19\n"
                       "#0 %3(24): App\\Services\\Example->run()\n"
                       "#1 %1(12): App\\Services\\Example->run()\n")
            .arg(source, source, frame)
            .toUtf8();
    const auto diagnostics =
        forge::core::DiagnosticsModel::parseLaravelLog(log, contents, directory.path());
    ASSERT_EQ(diagnostics.size(), 1);
    EXPECT_EQ(diagnostics.first().file, QDir::cleanPath(source));
    EXPECT_EQ(diagnostics.first().line, 19);
    EXPECT_EQ(diagnostics.first().source, QStringLiteral("laravel.log"));
    EXPECT_EQ(diagnostics.first().message,
              QStringLiteral("App\\Exceptions\\FailureException: Runtime failure"));
    ASSERT_EQ(diagnostics.first().frames.size(), 2);
    EXPECT_EQ(diagnostics.first().frames.first().file, QDir::cleanPath(frame));
    EXPECT_TRUE(forge::core::DiagnosticsModel::parseLaravelLog(log, {}, directory.path()).isEmpty());
}

TEST(Extensions, ImportsRealDeflatedOpenVsxThemeAndRetainsItsLicense) {
    QFile archive(QStringLiteral(FORGE_TEST_SOURCE_DIR) +
                  "/fixtures/extensions/onedark-2.3.0.vsix");
    ASSERT_TRUE(archive.open(QIODevice::ReadOnly));
    QTemporaryDir directory;
    forge::core::ExtensionManager manager(directory.path());
    QString error;
    ASSERT_TRUE(manager.installArchive(archive.readAll(), &error)) << error.toStdString();
    ASSERT_EQ(manager.installed().size(), 1);
    const auto manifest = manager.installed().first().toObject();
    EXPECT_EQ(manifest.value("id").toString(), "akamud.vscode-theme-onedark");
    EXPECT_EQ(manifest.value("license").toString(), "MIT");
    QFile license(directory.filePath("akamud.vscode-theme-onedark/LICENSE.txt"));
    ASSERT_TRUE(license.open(QIODevice::ReadOnly));
    EXPECT_TRUE(license.readAll().contains("Mahmoud Ali"));
    const auto theme = forge::core::ThemeCatalog::readJson(
        directory.filePath("akamud.vscode-theme-onedark/theme-0.json"));
    EXPECT_EQ(theme.value("name").toString(), "Atom One Dark");
    EXPECT_FALSE(theme.value("syntax").toObject().isEmpty());
}
