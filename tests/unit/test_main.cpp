/**
 * Forge IDE C++ unit tests — entry point.
 * Individual test files are added here as milestones progress.
 *
 * M0: Smoke test only (verify the test harness works).
 */
#include <gtest/gtest.h>

#include <iostream>
#include <limits>
#include <QAction>
#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QProcess>
#include <QString>
#include <QThread>
#include <QTimer>
#include <string>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

#include "forge/core/ActionManager.h"
#include "forge/core/KeymapManager.h"
#include "forge/core/RpcClient.h"
#include "forge/core/ServiceSupervisor.h"
#include "forge/core/TextFileCodec.h"

namespace {

int runChildFixture(const QString& mode) {
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (mode == QStringLiteral("--service-fixture")) {
        std::cin.ignore(std::numeric_limits<std::streamsize>::max());
        return 0;
    }
    if (mode == QStringLiteral("--crash-fixture")) {
        return 1;
    }
    if (mode == QStringLiteral("--rpc-oversized")) {
        std::cout << "Content-Length: 67108865\r\n\r\n" << std::flush;
        QThread::msleep(200);
        return 0;
    }

    QByteArray header;
    while (!header.endsWith("\r\n\r\n")) {
        std::string line;
        if (!std::getline(std::cin, line)) {
            return 3;
        }
        header.append(line.data(), static_cast<qsizetype>(line.size()));
        header.append('\n');
    }
    qlonglong contentLength = -1;
    for (const QByteArray& line : header.split('\n')) {
        if (line.trimmed().left(15).compare("Content-Length:", Qt::CaseInsensitive) == 0) {
            bool ok = false;
            contentLength = line.mid(line.indexOf(':') + 1).trimmed().toLongLong(&ok);
            if (!ok) {
                return 4;
            }
            break;
        }
    }
    if (contentLength <= 0 || contentLength > 1024 * 1024) {
        return 5;
    }
    std::string body(static_cast<size_t>(contentLength), '\0');
    std::cin.read(body.data(), contentLength);
    if (std::cin.gcount() != contentLength) {
        return 6;
    }
    const QJsonObject request =
        QJsonDocument::fromJson(QByteArray(body.data(), static_cast<qsizetype>(body.size())))
            .object();
    if (request.value(QStringLiteral("method")) == QStringLiteral("$/exit")) {
        return 0;
    }

    const QJsonObject response{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), request.value(QStringLiteral("id"))},
        {QStringLiteral("result"), QJsonObject{{QStringLiteral("pong"), true}}}};
    const QByteArray responseBody = QJsonDocument(response).toJson(QJsonDocument::Compact);
    const QByteArray frame = QByteArrayLiteral("Content-Length: ") +
                             QByteArray::number(responseBody.size()) +
                             QByteArrayLiteral("\r\n\r\n") + responseBody;
    std::cout.write(frame.constData(), frame.size());
    std::cout.flush();
    if (!std::cout) return 7;
    QThread::msleep(100);
    return 0;
}

void waitForEventLoop(QEventLoop& loop, int timeoutMs = 5000) {
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(timeoutMs);
    loop.exec();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 &&
        (QByteArray(argv[1]) == "--service-fixture" || QByteArray(argv[1]) == "--rpc-fixture" ||
         QByteArray(argv[1]) == "--crash-fixture" || QByteArray(argv[1]) == "--rpc-oversized")) {
        return runChildFixture(QString::fromLocal8Bit(argv[1]));
    }

    Q_INIT_RESOURCE(resources);
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
    QAction* action =
        actions.registerAction(QStringLiteral("forge.test.action"), QStringLiteral("Test Action"),
                               [&callCount]() { ++callCount; });

    ASSERT_NE(action, nullptr);
    action->trigger();
    EXPECT_EQ(callCount, 1);
}

TEST(ActionManager, AppliesKeymapToRegisteredAction) {
    forge::core::ActionManager actions;
    QAction* action = actions.registerAction(QStringLiteral("forge.test.action"),
                                             QStringLiteral("Test Action"), []() {});

    actions.applyKeymap({{QStringLiteral("forge.test.action"), QStringLiteral("Ctrl+S")}});

    ASSERT_NE(action, nullptr);
    EXPECT_EQ(action->shortcut().toString(), QStringLiteral("Ctrl+S"));
}

TEST(KeymapManager, LoadsBundledDefaultKeymapAndAppliesBindings) {
    forge::core::ActionManager actions;
    QAction* saveAction =
        actions.registerAction(QStringLiteral("forge.file.save"), QStringLiteral("Save"), []() {});
    forge::core::KeymapManager keymaps(&actions);

    ASSERT_TRUE(keymaps.loadDefaultKeymap());

    EXPECT_EQ(keymaps.activeKeymapName(), QStringLiteral("Default (PhpStorm-compatible)"));
    EXPECT_EQ(saveAction->shortcut().toString(), QStringLiteral("Ctrl+S"));
}

TEST(ThemeResources, BundledThemesAreValidJsonWithEditorPalette) {
    for (const QString& path :
         {QStringLiteral(":/themes/graphite.json"), QStringLiteral(":/themes/daylight.json")}) {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::ReadOnly)) << path.toStdString();
        QJsonParseError error;
        const QJsonDocument theme = QJsonDocument::fromJson(file.readAll(), &error);
        ASSERT_EQ(error.error, QJsonParseError::NoError) << error.errorString().toStdString();
        ASSERT_TRUE(theme.isObject());
        EXPECT_TRUE(theme.object()
                        .value(QStringLiteral("editor"))
                        .toObject()
                        .contains(QStringLiteral("background")));
    }
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

    QKeyEvent keyPress(QEvent::KeyPress, Qt::Key_N, Qt::ControlModifier, QStringLiteral("n"));
    QApplication::sendEvent(&editor, &keyPress);

    EXPECT_EQ(callCount, 1);
    window.close();
}

TEST(TextFileCodec, RoundTripsSupportedEncodingsAndLineEndings) {
    using forge::core::LineEnding;
    using forge::core::TextEncoding;
    const QVector<QVector<LineEnding>> endings{
        {LineEnding::Lf, LineEnding::Lf},
        {LineEnding::CrLf, LineEnding::CrLf},
        {LineEnding::Cr, LineEnding::Cr},
        {LineEnding::CrLf, LineEnding::Lf},
    };
    QVector<TextEncoding> encodings{
        TextEncoding::Utf8,
        TextEncoding::Utf8Bom,
        TextEncoding::Utf16LittleEndian,
        TextEncoding::Utf16BigEndian,
    };
#ifdef Q_OS_WIN
    encodings.append(TextEncoding::Windows1256);
    encodings.append(TextEncoding::Windows1252);
#endif

    for (TextEncoding encoding : encodings) {
        const QString text =
            encoding == TextEncoding::Windows1252
                ? QStringLiteral("Caf\u00e9\nHello\nForge")
                : (encoding == TextEncoding::Windows1256
                       ? QStringLiteral("\u0645\u0631\u062d\u0628\u0627\n"
                                        "\u0627\u0644\u0639\u0627\u0644\u0645\nPHP")
                       : QStringLiteral("\u0645\u0631\u062d\u0628\u0627\n"
                                        "\u4e16\u754c\nForge"));
        for (const QVector<LineEnding>& lineEndings : endings) {
            QByteArray bytes;
            QString error;
            ASSERT_TRUE(
                forge::core::TextFileCodec::encode(text, encoding, lineEndings, &bytes, &error))
                << error.toStdString();

            forge::core::DecodedText decoded;
            ASSERT_TRUE(forge::core::TextFileCodec::decode(bytes, &decoded, &error))
                << error.toStdString();
            EXPECT_EQ(decoded.text, text);
            EXPECT_EQ(decoded.encoding, encoding);
            EXPECT_EQ(decoded.lineEndings, lineEndings);
        }
    }
}

TEST(TextFileCodec, RejectsUnpairedUtf16Surrogates) {
    forge::core::DecodedText decoded;
    QString error;

    EXPECT_FALSE(
        forge::core::TextFileCodec::decode(QByteArray::fromHex("fffe00d8"), &decoded, &error));
    EXPECT_FALSE(error.isEmpty());
}

TEST(RpcClient, ResolvesResponseAndFailsPendingRequestOnProcessExit) {
    {
        QProcess process;
        forge::core::RpcClient client(&process);
        bool receivedResponse = false;
        QEventLoop responseLoop;
        const auto startConnection = QObject::connect(&process, &QProcess::started, &client, [&]() {
            client.sendRequest(QStringLiteral("$/ping"), {},
                               [&](const QJsonObject& result, const QJsonObject& error) {
                                   receivedResponse =
                                       result.value(QStringLiteral("pong")).toBool() &&
                                       error.isEmpty();
                                   responseLoop.quit();
                               });
        });
        process.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--rpc-fixture")});
        if (!receivedResponse) {
            waitForEventLoop(responseLoop);
        }
        QObject::disconnect(startConnection);
        EXPECT_TRUE(receivedResponse);
        if (process.state() != QProcess::NotRunning) {
            process.waitForFinished(1000);
        }
    }

    QProcess process;
    forge::core::RpcClient client(&process);
    bool receivedFailure = false;
    QEventLoop failureLoop;
    QObject::connect(&process, &QProcess::started, &client, [&]() {
        client.sendRequest(QStringLiteral("$/exit"), {},
                           [&](const QJsonObject& result, const QJsonObject& error) {
                               receivedFailure = result.isEmpty() && !error.isEmpty();
                               failureLoop.quit();
                           });
    });
    process.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--rpc-fixture")});
    if (!receivedFailure) {
        waitForEventLoop(failureLoop);
    }
    EXPECT_TRUE(receivedFailure);
}

TEST(RpcClient, RejectsOversizedFrameAndFailsPendingRequest) {
    QProcess process;
    forge::core::RpcClient client(&process);
    bool receivedFailure = false;
    QEventLoop loop;
    QObject::connect(&process, &QProcess::started, &client, [&]() {
        client.sendRequest(QStringLiteral("$/ping"), {},
                           [&](const QJsonObject& result, const QJsonObject& error) {
                               receivedFailure = result.isEmpty() && !error.isEmpty();
                               loop.quit();
                           });
    });
    process.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--rpc-oversized")});
    if (!receivedFailure) {
        waitForEventLoop(loop);
    }
    EXPECT_TRUE(receivedFailure);
    process.kill();
    process.waitForFinished(1000);
}

TEST(ServiceSupervisor, StopsServiceAfterClosingItsInputChannel) {
    forge::core::ServiceSupervisor supervisor;
    QEventLoop startLoop;
    bool started = false;
    QObject::connect(&supervisor, &forge::core::ServiceSupervisor::serviceStarted, &supervisor,
                     [&](const QString&) {
                         started = true;
                         startLoop.quit();
                     });
    forge::core::ServiceSupervisor::ServiceConfig config;
    config.key = QStringLiteral("fixture");
    config.executable = QCoreApplication::applicationFilePath();
    config.arguments = {QStringLiteral("--service-fixture")};
    config.healthyUptimeMs = 100;
    supervisor.registerService(config);
    if (!started) {
        waitForEventLoop(startLoop);
    }
    ASSERT_TRUE(started);

    QEventLoop stopLoop;
    QObject::connect(&supervisor, &forge::core::ServiceSupervisor::allServicesStopped, &stopLoop,
                     &QEventLoop::quit);
    supervisor.stopAll();
    if (!supervisor.allStopped()) {
        waitForEventLoop(stopLoop);
    }
    EXPECT_TRUE(supervisor.allStopped());
    EXPECT_EQ(supervisor.state(QStringLiteral("fixture")),
              forge::core::ServiceSupervisor::ServiceState::Stopped);
}

TEST(ServiceSupervisor, DisablesServiceAfterRestartLimit) {
    forge::core::ServiceSupervisor supervisor;
    QEventLoop loop;
    bool disabled = false;
    QObject::connect(&supervisor, &forge::core::ServiceSupervisor::serviceDisabled, &supervisor,
                     [&](const QString&, const QString&) {
                         disabled = true;
                         loop.quit();
                     });
    forge::core::ServiceSupervisor::ServiceConfig config;
    config.key = QStringLiteral("fixture");
    config.executable = QCoreApplication::applicationFilePath();
    config.arguments = {QStringLiteral("--crash-fixture")};
    config.maxRestarts = 1;
    config.backoffMs = 1;
    config.healthyUptimeMs = 100;
    supervisor.registerService(config);
    if (!disabled) {
        waitForEventLoop(loop);
    }

    EXPECT_TRUE(disabled);
    EXPECT_EQ(supervisor.state(QStringLiteral("fixture")),
              forge::core::ServiceSupervisor::ServiceState::Disabled);
    supervisor.stopService(QStringLiteral("fixture"));
    EXPECT_TRUE(supervisor.allStopped());
}
