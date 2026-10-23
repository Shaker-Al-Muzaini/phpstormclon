#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QString>
#include <QHash>
#include <QFile>
#include <memory>

namespace forge::core {

/**
 * @brief Manages lifecycle of Rust service sub-processes.
 *
 * Each service is started as a child process. If the service crashes,
 * ServiceSupervisor restarts it with exponential back-off. A crashing
 * service MUST NOT crash the UI process.
 *
 * Services are identified by a string key (e.g. "phpintel", "vcs").
 */
class ServiceSupervisor : public QObject {
    Q_OBJECT
public:
    struct ServiceConfig {
        QString key;             ///< Unique identifier (e.g. "phpintel")
        QString executable;      ///< Absolute path to the service binary
        QStringList arguments;   ///< Command-line arguments
        int maxRestarts  = 5;    ///< Give up after this many rapid crashes
        int backoffMs    = 500;  ///< Initial restart delay (doubles each attempt)
        int healthyUptimeMs = 60000; ///< Reset crash backoff after healthy uptime
    };

    enum class ServiceState { Stopped, Starting, Running, Crashed, Disabled };

    explicit ServiceSupervisor(QObject* parent = nullptr);
    ~ServiceSupervisor() override;

    /// Register and immediately start a service.
    void registerService(const ServiceConfig& cfg);

    /// Stop a service gracefully (SIGTERM, then SIGKILL after timeout).
    void stopService(const QString& key);

    /// Stop all services.
    void stopAll();

    /// Query current state.
    ServiceState state(const QString& key) const;
    QProcess* process(const QString& key) const;
    bool allStopped() const;

signals:
    void serviceStarted(const QString& key);
    void serviceStateChanged(const QString& key, ServiceState state);
    void serviceCrashed(const QString& key, int exitCode);
    void serviceDisabled(const QString& key, const QString& reason);
    void allServicesStopped();

private slots:
    void onProcessFinished(const QString& key, int exitCode, QProcess::ExitStatus status);
    void onProcessError(const QString& key, QProcess::ProcessError error);

private:
    struct Entry {
        ServiceConfig  config;
        QProcess*      process       = nullptr;
        ServiceState   state         = ServiceState::Stopped;
        int            restartCount  = 0;
        int            currentBackMs = 0;
        QTimer*        restartTimer  = nullptr;
        QTimer*        healthyTimer = nullptr;
        QTimer*        terminateTimer = nullptr;
        QTimer*        killTimer = nullptr;
        QFile*         stderrLog = nullptr;
    };

    void startProcess(Entry& entry);
    void scheduleRestart(Entry& entry);
    void finishStop(Entry& entry);
    void emitAllStoppedIfReady();

    QHash<QString, Entry> m_entries;
};

} // namespace forge::core
