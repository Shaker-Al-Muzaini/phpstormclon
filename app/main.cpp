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
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QIcon>
#include <QLocale>
#include <QStandardPaths>
#include <QTimer>
#include <QTranslator>

#include "BrandSplash.h"
#include "forge/core/ActionManager.h"
#include "forge/core/ServiceSupervisor.h"
#include "forge/core/Settings.h"
#include "MainWindow.h"

int main(int argc, char* argv[]) {
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
    const QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                           QStringLiteral("/logs");
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

    BrandSplash splash;
    if (!app.arguments().contains("--no-splash")) {
        splash.show();
        app.processEvents();
    }
    // Keep branding readable without blocking workspace initialization or the event loop.
    MainWindow window(&actionManager, &supervisor);
    auto option = [&app](const QString& key) {
        const qsizetype i = app.arguments().indexOf(key);
        return i >= 0 && i + 1 < app.arguments().size() ? app.arguments().at(i + 1) : QString();
    };
    if (!option("--project").isEmpty()) window.openProjectPath(option("--project"));
    if (!option("--file").isEmpty()) window.openDocument(option("--file"));
    if (!option("--theme").isEmpty()) window.selectTheme(option("--theme"));
    if (!option("--splash-screenshot").isEmpty())
        BrandSplash::artwork().save(option("--splash-screenshot"));
    if (app.arguments().contains("--rtl")) window.setLayoutDirection(Qt::RightToLeft);
    window.show();
    if (!app.arguments().contains("--no-splash"))
        QTimer::singleShot(650, &splash, [&splash, &window]() { splash.finish(&window); });
    if (app.arguments().contains("--screenshot")) {
        const qsizetype i = app.arguments().indexOf("--screenshot");
        if (i + 1 < app.arguments().size())
            QTimer::singleShot(700, &window, [&window, &app, i]() {
                app.exit(window.grab().save(app.arguments().at(i + 1)) ? 0 : 2);
            });
    }

    return app.exec();
}
