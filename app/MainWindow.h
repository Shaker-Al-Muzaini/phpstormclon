#pragma once

#include <QMainWindow>
#include <QHash>
#include <QList>
#include <QStringList>
#include <memory>

#include "forge/core/ServiceSupervisor.h"

class QFileSystemModel;
class QFileIconProvider;
class QLabel;
class QPlainTextEdit;
class QProcess;
class QProcessEnvironment;
class QComboBox;
class QStackedWidget;
class QTabWidget;
class QTreeView;
class QTcpServer;
class QTcpSocket;
class QWidget;

namespace forge::core {
class ActionManager;
class KeymapManager;
class RpcClient;
class Settings;
}

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
    void setupActions();
    void setupMenus();
    void setupToolbars();
    void setupToolWindows();
    void setupShortcuts();
    void setupStatusBar();
    void setupServices();
    void startTerminal(QProcess* process = nullptr, QPlainTextEdit* output = nullptr);
    void createTerminalSession();
    void closeTerminalSession(int index);
    void setActiveTerminalSession(int index);
    void submitTerminalCommand(const QString& command, QPlainTextEdit* output = nullptr);
    void appendTerminalOutput(const QString& text, QPlainTextEdit* output = nullptr);
    void findInCurrentDocument();
    void findNextInCurrentDocument();
    void goToLine();
    void goToFile();
    void showRecentFiles();
    void runCurrentPhpFile();
    void debugCurrentPhpFile();
    void profileCurrentPhpFile();
    void sendNextDebuggerCommand();
    void sendDebuggerStep(const QString& command);
    void showGitStatus();
    void runExternalCommand(const QString& program, const QStringList& arguments,
                            const QString& description,
                            const QProcessEnvironment* environment = nullptr);
    void openProject();
    void setProjectRoot(const QString& path);
    void openFile(const QModelIndex& index);
    void openFilePath(const QString& path);
    void createUntitledDocument();
    bool saveCurrentDocument();
    bool saveDocument(QPlainTextEdit* editor);
    void closeDocument(int index);
    void applyTheme(const QString& name);
    void showSettings();
    void showPluginManager();
    void restoreDefaultLayout();
    void applyEditorTheme(QPlainTextEdit* editor);
    void updateEditorStatus();
    QPlainTextEdit* currentEditor() const;

    forge::core::ActionManager* m_actions;
    forge::core::ServiceSupervisor* m_supervisor;
    std::unique_ptr<forge::core::RpcClient> m_phpIntelClient;
    std::unique_ptr<forge::core::KeymapManager> m_keymapManager;
    std::unique_ptr<QFileIconProvider> m_fileIconProvider;

    QFileSystemModel* m_fileSystemModel = nullptr;
    QTreeView* m_projectTree = nullptr;
    QTabWidget* m_editorTabs = nullptr;
    QTabWidget* m_terminalTabs = nullptr;
    QStackedWidget* m_centerStack = nullptr;
    QWidget* m_welcomePage = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_caretLabel = nullptr;
    QLabel* m_fileTypeLabel = nullptr;
    QLabel* m_projectLabel = nullptr;
    QComboBox* m_runConfiguration = nullptr;
    QPlainTextEdit* m_terminalOutput = nullptr;
    QProcess* m_terminalProcess = nullptr;
    QList<QPlainTextEdit*> m_terminalViews;
    QList<QProcess*> m_terminalProcesses;
    QHash<QProcess*, QStringList> m_pendingTerminalCommands;
    QProcess* m_commandProcess = nullptr;
    QTcpServer* m_debugServer = nullptr;
    QTcpSocket* m_debugSocket = nullptr;
    QByteArray m_debugBuffer;
    QStringList m_debugCommands;
    int m_debugTransactionId = 1;
    QString m_profileDirectory;
    bool m_profileAfterRun = false;
    qint64 m_profileStartedAt = 0;
    QString m_projectRoot;
    QString m_themeName = QStringLiteral("Graphite");
    QString m_lastFindQuery;
    QStringList m_recentFiles;
};
