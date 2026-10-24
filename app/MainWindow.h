#pragma once

#include <memory>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QStringList>

#include "forge/core/DiagnosticsModel.h"
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
class QToolButton;
class QTreeWidget;
class QMenu;
class QLineEdit;
class FindBar;
class QSplitter;
class ProblemsPanel;

namespace forge::core {
class ActionManager;
class KeymapManager;
class RpcClient;
class Settings;
class ProjectSearch;
class ExtensionManager;
class FileWatcher;
}  // namespace forge::core

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(forge::core::ActionManager* actions,
                        forge::core::ServiceSupervisor* supervisor, QWidget* parent = nullptr);
    ~MainWindow() override;
    void openProjectPath(const QString& path) {
        setProjectRoot(path);
    }
    void openDocument(const QString& path) {
        openFilePath(path);
    }
    void selectTheme(const QString& name) {
        applyTheme(name);
    }

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* object, QEvent* event) override;

private slots:
    void onServiceStateChanged(const QString& key,
                               forge::core::ServiceSupervisor::ServiceState state);
    void onPingResponse(const QJsonObject& result, const QJsonObject& error);

private:
    void setupActions();
    void setupWorkspaceFeatures();
    void showQuickOpen(bool actionsOnly = false);
    void splitEditor(Qt::Orientation orientation);
    void syncSplitEditor();
    void decorateEditorTab(QPlainTextEdit* editor);
    void showFindInFiles();
    void refreshThemeMenus();
    void applyWorkspaceStyle(const QJsonObject& theme);
    void refreshExtensionBindings();
    void refreshBranch();
    void insertSnippet();
    void editLine(const QString& operation);
    void projectContextMenu(const QPoint& position);
    void reloadDocument(QPlainTextEdit* editor);
    bool confirmExternalChange(QPlainTextEdit* editor, const QString& path);
    void updateDocumentInfo();
    void updateDiagnostics();
    void updateProblemsCurrentDocument();
    void connectPhpIntelDocument(QPlainTextEdit* editor);
    QPlainTextEdit* primaryEditor(QPlainTextEdit* editor) const;
    int documentVersion(QPlainTextEdit* editor) const;
    void closePhpIntelDocument(QPlainTextEdit* editor);
    void sendPhpIntelDidOpen(QPlainTextEdit* editor);
    void sendPhpIntelDidChange(QPlainTextEdit* editor);
    void handlePhpIntelDiagnostics(const QJsonObject& params);
    void runPhpSyntaxCheck(const QString& path);
    void updateLaravelLogDiagnostics();
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
    forge::core::DiagnosticsModel m_diagnostics;
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
    QToolButton* m_problemCountButton = nullptr;
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
    FindBar* m_findBar = nullptr;
    QSplitter* m_editorSplitter = nullptr;
    QPlainTextEdit* m_splitEditor = nullptr;
    forge::core::ProjectSearch* m_projectSearch = nullptr;
    forge::core::ExtensionManager* m_extensions = nullptr;
    forge::core::FileWatcher* m_fileWatcher = nullptr;
    ProblemsPanel* m_problemsPanel = nullptr;
    QTimer* m_laravelLogDebounce = nullptr;
    QString m_laravelLogPath;
    QHash<QString, QProcess*> m_phpLintProcesses;
    bool m_phpLintEnabled = true;
    QMenu* m_themeMenu = nullptr;
    QToolButton* m_projectButton = nullptr;
    QToolButton* m_branchButton = nullptr;
    QToolButton* m_encodingButton = nullptr;
    QToolButton* m_eolButton = nullptr;
    QToolButton* m_indentButton = nullptr;
    QToolButton* m_readOnlyButton = nullptr;
    QLabel* m_breadcrumbs = nullptr;
    QElapsedTimer m_shiftTimer;
    bool m_shiftOnly = false;
    QString m_lastFindQuery;
    QStringList m_recentFiles;
    bool m_servicesStopping = false;
};
