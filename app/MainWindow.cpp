#include "MainWindow.h"
#include "forge/core/ActionManager.h"
#include "forge/core/ServiceSupervisor.h"
#include "forge/core/RpcClient.h"
#include "forge/core/Settings.h"

#include <QApplication>
#include <QLabel>
#include <QProcess>
#include <QStatusBar>
#include <QCloseEvent>
#include <QVBoxLayout>
#include <QWidget>
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
#include <QJsonObject>

MainWindow::MainWindow(forge::core::ActionManager* actions,
                       forge::core::ServiceSupervisor* supervisor,
                       QWidget* parent)
    : QMainWindow(parent)
    , m_actions(actions)
    , m_supervisor(supervisor)
{
    setWindowTitle(QStringLiteral("%1 %2")
                   .arg(QLatin1String(FORGE_PRODUCT_NAME),
                        QLatin1String(FORGE_VERSION)));
    resize(1280, 800);
    setMinimumSize(800, 600);

    // Central placeholder (replaced by editor + docking in M1/M2)
    auto* central = new QWidget(this);
    auto* layout  = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* placeholder = new QLabel(
        QStringLiteral("<center>"
                       "<h2 style='color:#6C9EE8;'>" FORGE_PRODUCT_NAME "</h2>"
                       "<p style='color:#888;'>v" FORGE_VERSION " &mdash; Milestone 0</p>"
                       "<p style='color:#666; font-size:11px;'>Services starting&hellip;</p>"
                       "</center>"), central);
    placeholder->setAlignment(Qt::AlignCenter);
    layout->addWidget(placeholder);
    setCentralWidget(central);

    setupStatusBar();
    setupServices();

    // Register a trivial action so we can verify ActionManager works
    m_actions->registerAction(
        QStringLiteral("forge.app.quit"),
        QStringLiteral("Quit"),
        [this]() { close(); }
    );

    qInfo() << "[MainWindow] Created.";
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event)
{
    m_supervisor->stopAll();
    if (forge::core::Settings::instance()) {
        forge::core::Settings::instance()->save();
    }
    event->accept();
}

void MainWindow::setupStatusBar()
{
    m_statusLabel = new QLabel(QStringLiteral(" Services: starting\u2026"), this);
    statusBar()->addPermanentWidget(m_statusLabel);
    statusBar()->showMessage(QStringLiteral(FORGE_PRODUCT_NAME " ready."), 3000);
}

void MainWindow::setupServices()
{
    connect(m_supervisor, &forge::core::ServiceSupervisor::serviceStateChanged,
            this, &MainWindow::onServiceStateChanged);

    // Resolve phpintel binary path: next to our executable
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString phpIntelBin = exeDir + QStringLiteral("/phpintel.exe");

    if (!QDir::root().exists(phpIntelBin)) {
        qWarning() << "[MainWindow] phpintel binary not found at" << phpIntelBin
                   << "(expected after Rust build)";
        m_statusLabel->setText(QStringLiteral(" phpintel: not built yet"));
        return;
    }

    forge::core::ServiceSupervisor::ServiceConfig cfg;
    cfg.key        = QStringLiteral("phpintel");
    cfg.executable = phpIntelBin;
    cfg.arguments  = { QStringLiteral("--stdio") };
    m_supervisor->registerService(cfg);

    // After service is registered, QProcess is available via supervisor internals
    // For M0 we skip actual ping since service binary may not exist yet.
}

void MainWindow::onServiceStateChanged(const QString& key,
                                       forge::core::ServiceSupervisor::ServiceState state)
{
    using State = forge::core::ServiceSupervisor::ServiceState;
    QString stateStr;
    switch (state) {
    case State::Starting: stateStr = QStringLiteral("starting"); break;
    case State::Running:  stateStr = QStringLiteral("running"); break;
    case State::Crashed:  stateStr = QStringLiteral("crashed"); break;
    case State::Disabled: stateStr = QStringLiteral("disabled"); break;
    case State::Stopped:  stateStr = QStringLiteral("stopped"); break;
    }
    m_statusLabel->setText(QStringLiteral(" %1: %2").arg(key, stateStr));
    qInfo() << "[MainWindow] Service" << key << "->" << stateStr;
}

void MainWindow::onPingResponse(const QJsonObject& result, const QJsonObject& error)
{
    if (!error.isEmpty()) {
        qWarning() << "[MainWindow] Ping error:" << error;
    } else {
        qInfo() << "[MainWindow] Ping OK:" << result;
    }
}
