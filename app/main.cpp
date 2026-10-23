/**
 * Forge IDE — Application entry point
 *
 * Responsibilities:
 *   1. Set up Qt application metadata.
 *   2. Load settings.
 *   3. Create the main window.
 *   4. Start the service supervisor (Rust services).
 *   5. Enter the Qt event loop.
 *
 * A crash in a Rust service must NEVER propagate here. The ServiceSupervisor
 * handles restarts with exponential back-off. This process only exits when
 * the user closes the window or calls QCoreApplication::quit().
 */

#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QDir>
#include <QIcon>
#include <QStandardPaths>
#include <QDebug>

#include "MainWindow.h"
#include "forge/core/Settings.h"
#include "forge/core/ServiceSupervisor.h"
#include "forge/core/ActionManager.h"

int main(int argc, char* argv[])
{
    // High-DPI: let Qt handle it automatically (Qt 6 default)
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral(FORGE_PRODUCT_NAME));
    app.setApplicationVersion(QStringLiteral(FORGE_VERSION));
    app.setOrganizationName(QStringLiteral("ForgeIDE"));
    app.setOrganizationDomain(QStringLiteral("forge-ide.dev"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/forge.svg")));

    // Ensure log directory exists
    const QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                           + QStringLiteral("/logs");
    QDir().mkpath(logDir);

    // Settings (singleton)
    forge::core::Settings settings;

    // Action manager (singleton-like, passed to MainWindow)
    forge::core::ActionManager actionManager;

    // Service supervisor
    forge::core::ServiceSupervisor supervisor;
    // Services are registered in MainWindow after the binary paths are resolved

    // i18n: load translation for the system locale
    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString& locale : uiLanguages) {
        const QString baseName = QStringLiteral("ForgeIDE_") + QLocale(locale).name();
        if (translator.load(QStringLiteral(":/i18n/") + baseName)) {
            app.installTranslator(&translator);
            qDebug() << "[main] Loaded translation:" << baseName;
            break;
        }
    }

    // Create and show main window
    MainWindow window(&actionManager, &supervisor);
    window.show();

    return app.exec();
}
