#include "MainWindow.h"

#include <QTimer>

#include "DocumentCodec.h"
#include "EditorWidgets.h"
#include "FindBar.h"
#include "forge/core/ExtensionManager.h"
#include "forge/core/FileWatcher.h"
#include "forge/core/ProjectSearch.h"
#include "TerminalView.h"
#include "WorkspaceSupport.h"

constexpr int kWindowLayoutVersion = 3;

MainWindow::MainWindow(forge::core::ActionManager* actions,
                       forge::core::ServiceSupervisor* supervisor, QWidget* parent)
    : QMainWindow(parent), m_actions(actions), m_supervisor(supervisor), m_diagnostics(this) {
    setWindowTitle(QStringLiteral("%1 %2").arg(QLatin1String(FORGE_PRODUCT_NAME),
                                               QLatin1String(FORGE_VERSION)));
    setWindowIcon(forgeIcon(QStringLiteral("forge"), QColor(QStringLiteral("#DFE1E5")),
                            QColor(QStringLiteral("#4D78CC"))));
    setObjectName(QStringLiteral("ForgeMainWindow"));
    resize(1440, 900);
    setMinimumSize(900, 600);

    m_projectSearch = new forge::core::ProjectSearch(this);
    m_extensions = new forge::core::ExtensionManager({}, this);
    m_fileWatcher = new forge::core::FileWatcher(this);
    setupActions();
    setupToolWindows();
    setupMenus();
    setupToolbars();
    setupStatusBar();
    setupWorkspaceFeatures();

    m_keymapManager = std::make_unique<forge::core::KeymapManager>(m_actions, this);
    if (!m_keymapManager->loadDefaultKeymap()) {
        statusBar()->showMessage(QStringLiteral("Could not load the default keymap."), 8000);
    }
    setupShortcuts();
    refreshExtensionBindings();
    qApp->installEventFilter(this);

    auto* settings = forge::core::Settings::instance();
    const QString theme =
        settings
            ? settings->value(QStringLiteral("ui.theme"), QStringLiteral("Graphite")).toString()
            : QStringLiteral("Graphite");
    applyTheme(theme);

    if (settings) {
        const QByteArray savedState = QByteArray::fromBase64(
            settings->value(QStringLiteral("window.state")).toString().toLatin1());
        const QByteArray savedGeometry = QByteArray::fromBase64(
            settings->value(QStringLiteral("window.geometry")).toString().toLatin1());
        if (!savedState.isEmpty() && !restoreState(savedState, kWindowLayoutVersion)) {
            qWarning() << "[MainWindow] Saved window layout could not be restored.";
        }
        if (!savedGeometry.isEmpty() && !restoreGeometry(savedGeometry)) {
            qWarning() << "[MainWindow] Saved window geometry could not be restored.";
        }

        const QString previousProject =
            settings->value(QStringLiteral("project.lastPath")).toString();
        if (!previousProject.isEmpty() && QDir(previousProject).exists()) {
            setProjectRoot(previousProject);
        }
    }

    setupServices();
    connect(&m_diagnostics, &forge::core::DiagnosticsModel::diagnosticsChanged, this,
            &MainWindow::updateDiagnostics);
    connect(&m_diagnostics, &forge::core::DiagnosticsModel::statusMessageChanged, this,
            [this](const QString&) { updateDiagnostics(); });
    updateDiagnostics();
    connect(m_supervisor, &forge::core::ServiceSupervisor::allServicesStopped, this, [this]() {
        if (m_servicesStopping) {
            m_servicesStopping = false;
            close();
        }
    });
    updateEditorStatus();
    qInfo() << "[MainWindow] IDE shell initialized.";
}

MainWindow::~MainWindow() {
    qApp->removeEventFilter(this);
    // Stop child-process callbacks while this class's members are still alive.
    // QObject's child destruction happens after C++ members have been destroyed.
    m_supervisor->disconnect(this);
    for (auto* child : findChildren<QObject*>()) child->disconnect(this);
    for (auto* process : findChildren<QProcess*>()) {
        if (process->state() != QProcess::NotRunning) {
            process->kill();
            process->waitForFinished(1000);
        }
    }
    if (m_projectTree) {
        m_projectTree->setModel(nullptr);
    }
    delete m_fileSystemModel;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    for (int index = m_editorTabs->count() - 1; index >= 0; --index) {
        m_editorTabs->setCurrentIndex(index);
        QPlainTextEdit* editor = currentEditor();
        if (editor && editor->document()->isModified()) {
            const QMessageBox::StandardButton result = QMessageBox::warning(
                this, QStringLiteral("Unsaved changes"),
                QStringLiteral("Save changes to %1?")
                    .arg(m_editorTabs->tabText(index).remove(QLatin1Char('*'))),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
            if (result == QMessageBox::Cancel) {
                event->ignore();
                return;
            }
            if (result == QMessageBox::Save && !saveDocument(editor)) {
                event->ignore();
                return;
            }
        }
    }

    if (auto* settings = forge::core::Settings::instance()) {
        settings->setValue(QStringLiteral("window.state"),
                           QString::fromLatin1(saveState(kWindowLayoutVersion).toBase64()));
        settings->setValue(QStringLiteral("window.geometry"),
                           QString::fromLatin1(saveGeometry().toBase64()));
        if (!m_projectRoot.isEmpty())
            settings->setValue("layout." + m_projectRoot,
                               QString::fromLatin1(saveState(kWindowLayoutVersion).toBase64()));
        settings->save();
    }
    if (!m_supervisor->allStopped()) {
        m_servicesStopping = true;
        setEnabled(false);
        statusBar()->showMessage(QStringLiteral("Stopping background services..."));
        event->ignore();
        m_supervisor->stopAll();
        return;
    }
    for (QProcess* process : std::as_const(m_terminalProcesses)) {
        if (process && process->state() != QProcess::NotRunning) {
            process->terminate();
            if (!process->waitForFinished(1000)) {
                process->kill();
                process->waitForFinished(1000);
            }
        }
    }
    if (m_commandProcess && m_commandProcess->state() != QProcess::NotRunning) {
        m_commandProcess->terminate();
        if (!m_commandProcess->waitForFinished(1000)) {
            m_commandProcess->kill();
            m_commandProcess->waitForFinished(1000);
        }
    }
    event->accept();
}

void MainWindow::setupServices() {
    connect(m_supervisor, &forge::core::ServiceSupervisor::serviceStateChanged, this,
            &MainWindow::onServiceStateChanged);
    connect(m_supervisor, &forge::core::ServiceSupervisor::serviceStarted, this,
            [this](const QString& key) {
                if (key != QStringLiteral("phpintel")) {
                    return;
                }
                QProcess* process = m_supervisor->process(key);
                if (!process) {
                    qWarning() << "[MainWindow] phpintel process is not available after startup.";
                    m_statusLabel->setText(QStringLiteral("phpintel: unavailable"));
                    return;
                }
                if (!m_phpIntelClient) {
                    m_phpIntelClient = std::make_unique<forge::core::RpcClient>(process, this);
                    connect(m_phpIntelClient.get(), &forge::core::RpcClient::notificationReceived,
                            this, [this](const QString& method, const QJsonObject& params) {
                                if (method == QStringLiteral("textDocument/publishDiagnostics")) {
                                    handlePhpIntelDiagnostics(params);
                                }
                            });
                }
                for (auto* editor : findChildren<QPlainTextEdit*>()) {
                    if (editor->property("forgeSourceEditor").toBool()) {
                        editor->document()->setProperty("phpIntelUri", QString());
                    }
                }
                for (auto* editor : findChildren<QPlainTextEdit*>()) {
                    if (editor->property("forgeSourceEditor").toBool()) {
                        sendPhpIntelDidOpen(editor);
                    }
                }
                m_phpIntelClient->sendRequest(
                    QStringLiteral("$/ping"), {},
                    [this](const QJsonObject& result, const QJsonObject& error) {
                        onPingResponse(result, error);
                    });
            });

    const QString phpIntelBin = QCoreApplication::applicationDirPath() + QStringLiteral("/phpintel")
#ifdef Q_OS_WIN
                                + QStringLiteral(".exe")
#endif
        ;
    if (!QFileInfo::exists(phpIntelBin)) {
        qWarning() << "[MainWindow] phpintel binary not found at" << phpIntelBin;
        m_statusLabel->setText(QStringLiteral("phpintel: not built"));
        return;
    }

    forge::core::ServiceSupervisor::ServiceConfig cfg;
    cfg.key = QStringLiteral("phpintel");
    cfg.executable = phpIntelBin;
    cfg.arguments = {QStringLiteral("--stdio")};
    m_supervisor->registerService(cfg);
}

void MainWindow::onServiceStateChanged(const QString& key,
                                       forge::core::ServiceSupervisor::ServiceState state) {
    using State = forge::core::ServiceSupervisor::ServiceState;
    QString stateText;
    switch (state) {
        case State::Starting:
            stateText = QStringLiteral("starting");
            break;
        case State::Running:
            stateText = QStringLiteral("running");
            break;
        case State::Crashed:
            stateText = QStringLiteral("crashed");
            break;
        case State::Disabled:
            stateText = QStringLiteral("disabled");
            break;
        case State::Stopped:
            stateText = QStringLiteral("stopped");
            break;
    }
    m_statusLabel->setText(QStringLiteral("%1: %2").arg(key, stateText));
    qInfo() << "[MainWindow] Service" << key << "->" << stateText;
}

void MainWindow::onPingResponse(const QJsonObject& result, const QJsonObject& error) {
    if (!error.isEmpty()) {
        qWarning() << "[MainWindow] Ping error:" << error;
        m_statusLabel->setText(QStringLiteral("phpintel: handshake failed"));
    } else {
        qInfo() << "[MainWindow] Ping OK:" << result;
        if (result.value(QStringLiteral("pong")).toBool()) {
            m_statusLabel->setText(QStringLiteral("phpintel: ready %1")
                                       .arg(result.value(QStringLiteral("version")).toString()));
        } else {
            m_statusLabel->setText(QStringLiteral("phpintel: invalid handshake response"));
            qWarning() << "[MainWindow] Unexpected phpintel handshake response:" << result;
        }
    }
}
