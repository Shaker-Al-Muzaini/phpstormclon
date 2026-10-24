#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include "forge/core/PhpLanguageClient.h"
#include "forge/core/RpcClient.h"

namespace {
bool writeFile(const QString& path, const QByteArray& contents) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

QJsonValue query(forge::core::PhpLanguageClient& client, const QString& method,
                 const QJsonObject& parameters, QJsonObject* error) {
    QEventLoop loop;
    bool finished = false;
    QJsonValue result;
    client.request(
        method, parameters,
        [&](const QJsonValue& value, const QJsonObject& failure) {
            result = value;
            *error = failure;
            finished = true;
            loop.quit();
        },
        10000);
    QTimer::singleShot(12000, &loop, &QEventLoop::quit);
    if (!finished) loop.exec();
    if (!finished) *error = {{"message", "No language-server response"}};
    return result;
}
}  // namespace

TEST(RpcClient, PreservesArrayAndNullResults) {
    for (const auto& method : {QStringLiteral("fixture/array"), QStringLiteral("fixture/null")}) {
        QProcess process;
        forge::core::RpcClient rpc(&process);
        QEventLoop loop;
        QJsonValue result;
        QJsonObject error;
        bool received = false;
        QObject::connect(&process, &QProcess::started, &rpc, [&]() {
            rpc.sendRequestValue(method, {},
                                 [&](const QJsonValue& value, const QJsonObject& failure) {
                                     result = value;
                                     error = failure;
                                     received = true;
                                     loop.quit();
                                 });
        });
        process.start(QCoreApplication::applicationFilePath(), {"--rpc-fixture"});
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        EXPECT_TRUE(received);
        EXPECT_TRUE(error.isEmpty());
        if (method.endsWith("array"))
            EXPECT_EQ(result.toArray(), (QJsonArray{"first", "second"}));
        else
            EXPECT_TRUE(result.isNull());
        process.kill();
        process.waitForFinished(1000);
    }
}

TEST(RpcClient, TimesOutAnUnresponsiveService) {
    QProcess process;
    forge::core::RpcClient rpc(&process);
    QEventLoop loop;
    int callbacks = 0;
    int code = 0;
    QObject::connect(&process, &QProcess::started, &rpc, [&]() {
        rpc.sendRequestValue(
            "never-replies", {},
            [&](const QJsonValue&, const QJsonObject& error) {
                ++callbacks;
                code = error.value("code").toInt();
                loop.quit();
            },
            50);
    });
    process.start(QCoreApplication::applicationFilePath(), {"--service-fixture"});
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    EXPECT_EQ(callbacks, 1);
    EXPECT_EQ(code, -32001);
    process.kill();
    process.waitForFinished(1000);
    QTest::qWait(20);
    EXPECT_EQ(callbacks, 1);
}

TEST(PhpLanguageClient, RealServerCompletesMembersFindsDefinitionsAndReportsSemanticErrors) {
    const auto script = QString::fromLocal8Bit(qgetenv("FORGE_TEST_LSP_SCRIPT"));
    const auto node = QStandardPaths::findExecutable("node");
    if (!QFileInfo::exists(script) || node.isEmpty()) {
        ASSERT_FALSE(qEnvironmentVariableIsSet("FORGE_REQUIRE_LSP_TESTS"));
        GTEST_SKIP() << "Set FORGE_TEST_LSP_SCRIPT to an installed Intelephense server.";
    }
    QTemporaryDir project;
    QTemporaryDir cache;
    ASSERT_TRUE(project.isValid());
    ASSERT_TRUE(QDir(project.path()).mkdir("src"));
    ASSERT_TRUE(
        writeFile(project.filePath("composer.json"), R"({"autoload":{"psr-4":{"App\\":"src/"}}})"));
    const auto person = project.filePath("src/Person.php");
    ASSERT_TRUE(writeFile(person,
                          "<?php\nnamespace App;\nclass Person {\n"
                          " public function displayName(): string { return 'Ada'; }\n}\n"));
    const auto usage = project.filePath("src/Usage.php");
    const QString input = "<?php\nnamespace App;\n$p = new Person();\n$p->disp\n";
    ASSERT_TRUE(writeFile(usage, input.toUtf8()));
    forge::core::PhpLanguageClient client;
    forge::core::PhpLanguageClient::Configuration configuration;
    configuration.node = node;
    configuration.script = script;
    configuration.workspace = project.path();
    configuration.storage = cache.path();
    client.setDocument(usage, input, 1);
    client.start(configuration);
    QTRY_VERIFY_WITH_TIMEOUT(client.isReady(), 30000);
    QJsonObject error;
    QJsonArray items;
    for (int attempt = 0; attempt < 30 && items.isEmpty(); ++attempt) {
        const auto result = query(client, "textDocument/completion",
                                  {{"textDocument", QJsonObject{{"uri", client.uriForFile(usage)}}},
                                   {"position", QJsonObject{{"line", 3}, {"character", 8}}}},
                                  &error);
        ASSERT_TRUE(error.isEmpty()) << error.value("message").toString().toStdString();
        items = result.isArray() ? result.toArray() : result.toObject().value("items").toArray();
        if (items.isEmpty()) QTest::qWait(100);
    }
    bool memberFound = false;
    for (const auto& item : items)
        memberFound |= item.toObject().value("label").toString().startsWith("displayName");
    EXPECT_TRUE(memberFound);
    const auto definition = query(client, "textDocument/definition",
                                  {{"textDocument", QJsonObject{{"uri", client.uriForFile(usage)}}},
                                   {"position", QJsonObject{{"line", 2}, {"character", 12}}}},
                                  &error);
    ASSERT_TRUE(error.isEmpty());
    if (definition.isArray()) ASSERT_FALSE(definition.toArray().isEmpty());
    const auto location =
        definition.isArray() ? definition.toArray().first().toObject() : definition.toObject();
    EXPECT_EQ(location.value("uri").toString(location.value("targetUri").toString()),
              client.uriForFile(person));

    const auto duplicate = project.filePath("src/Duplicate.php");
    bool semanticError = false;
    bool cleared = false;
    QObject::connect(&client, &forge::core::PhpLanguageClient::diagnosticsReceived, &client,
                     [&](const QJsonObject& parameters) {
                         if (parameters.value("uri").toString() != client.uriForFile(duplicate))
                             return;
                         if (parameters.value("diagnostics").toArray().isEmpty())
                             cleared = semanticError;
                         else
                             semanticError = true;
                     });
    client.setDocument(duplicate, "<?php\nclass Duplicate { private $id; private $id; }\n", 1);
    QTRY_VERIFY_WITH_TIMEOUT(semanticError, 10000);
    client.setDocument(duplicate, "<?php\nclass Duplicate { private $id; }\n", 2);
    QTRY_VERIFY_WITH_TIMEOUT(cleared, 10000);
    client.closeDocument(duplicate);
    client.stop();
}
