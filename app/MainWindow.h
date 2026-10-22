#pragma once
#include <QMainWindow>
#include <memory>
#include "forge/core/ServiceSupervisor.h"

namespace forge::core {
    class ActionManager;
    class RpcClient;
    class Settings;
}

/**
 * @brief Forge IDE main window.
 *
 * Milestone 0: skeleton window with:
 *   - Title bar with product name and version
 *   - Central placeholder widget
 *   - Status bar
 *   - Service supervisor integration (starts phpintel, shows status)
 *
 * Full docking, menus, and tool windows are implemented in M1.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(forge::core::ActionManager* actions,
                        forge::core::ServiceSupervisor* supervisor,
                        QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onServiceStateChanged(const QString& key,
                               forge::core::ServiceSupervisor::ServiceState state);
    void onPingResponse(const QJsonObject& result, const QJsonObject& error);

private:
    void setupServices();
    void setupStatusBar();
    void sendPing();

    forge::core::ActionManager*    m_actions;
    forge::core::ServiceSupervisor* m_supervisor;
    std::unique_ptr<forge::core::RpcClient> m_phpIntelClient;

    class QLabel* m_statusLabel = nullptr;
    class QProcess* m_phpIntelProcess = nullptr;
};
