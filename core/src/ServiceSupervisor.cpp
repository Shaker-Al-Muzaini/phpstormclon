#include "forge/core/ServiceSupervisor.h"
#include <QDebug>
#include <QTimer>

namespace forge::core {

ServiceSupervisor::ServiceSupervisor(QObject* parent)
    : QObject(parent)
{}

ServiceSupervisor::~ServiceSupervisor()
{
    stopAll();
}

void ServiceSupervisor::registerService(const ServiceConfig& cfg)
{
    if (m_entries.contains(cfg.key)) {
        qWarning() << "[ServiceSupervisor] Service already registered:" << cfg.key;
        return;
    }

    Entry entry;
    entry.config        = cfg;
    entry.currentBackMs = cfg.backoffMs;
    m_entries.insert(cfg.key, entry);

    startProcess(m_entries[cfg.key]);
}

void ServiceSupervisor::stopService(const QString& key)
{
    if (!m_entries.contains(key)) return;
    auto& entry = m_entries[key];

    if (entry.restartTimer) {
        entry.restartTimer->stop();
    }

    if (entry.process && entry.process->state() != QProcess::NotRunning) {
        entry.state = ServiceState::Stopped;
        entry.process->terminate();
        if (!entry.process->waitForFinished(3000)) {
            entry.process->kill();
        }
    }
}

void ServiceSupervisor::stopAll()
{
    for (const auto& key : m_entries.keys()) {
        stopService(key);
    }
}

ServiceSupervisor::ServiceState ServiceSupervisor::state(const QString& key) const
{
    if (!m_entries.contains(key)) return ServiceState::Stopped;
    return m_entries.value(key).state;
}

void ServiceSupervisor::startProcess(Entry& entry)
{
    if (!entry.process) {
        entry.process = new QProcess(this);

        const QString& key = entry.config.key;

        connect(entry.process, &QProcess::finished,
                this, [this, key](int exitCode, QProcess::ExitStatus status) {
                    onProcessFinished(key, exitCode, status);
                });
        connect(entry.process, &QProcess::errorOccurred,
                this, [this, key](QProcess::ProcessError error) {
                    onProcessError(key, error);
                });
    }

    entry.state = ServiceState::Starting;
    emit serviceStateChanged(entry.config.key, entry.state);

    entry.process->start(entry.config.executable, entry.config.arguments);

    if (!entry.process->waitForStarted(5000)) {
        qCritical() << "[ServiceSupervisor] Failed to start service:" << entry.config.key;
        onProcessError(entry.config.key, QProcess::FailedToStart);
        return;
    }

    entry.state = ServiceState::Running;
    emit serviceStarted(entry.config.key);
    emit serviceStateChanged(entry.config.key, entry.state);
    qInfo() << "[ServiceSupervisor] Service started:" << entry.config.key
             << "PID:" << entry.process->processId();
}

void ServiceSupervisor::scheduleRestart(Entry& entry)
{
    if (entry.restartCount >= entry.config.maxRestarts) {
        entry.state = ServiceState::Disabled;
        const QString reason = QStringLiteral("Exceeded max restarts (%1)").arg(entry.config.maxRestarts);
        emit serviceDisabled(entry.config.key, reason);
        qCritical() << "[ServiceSupervisor] Service disabled:" << entry.config.key << reason;
        return;
    }

    ++entry.restartCount;
    const int delay = entry.currentBackMs;
    entry.currentBackMs = qMin(entry.currentBackMs * 2, 30000); // cap at 30 s

    qWarning() << "[ServiceSupervisor] Restarting" << entry.config.key
               << "in" << delay << "ms (attempt" << entry.restartCount << ")";

    if (!entry.restartTimer) {
        entry.restartTimer = new QTimer(this);
        entry.restartTimer->setSingleShot(true);
        const QString& key = entry.config.key;
        connect(entry.restartTimer, &QTimer::timeout, this, [this, key]() {
            if (m_entries.contains(key)) {
                startProcess(m_entries[key]);
            }
        });
    }
    entry.restartTimer->start(delay);
}

void ServiceSupervisor::onProcessFinished(const QString& key, int exitCode, QProcess::ExitStatus status)
{
    if (!m_entries.contains(key)) return;
    auto& entry = m_entries[key];

    if (entry.state == ServiceState::Stopped) return; // Intentional stop

    qWarning() << "[ServiceSupervisor] Service" << key
               << "exited with code" << exitCode
               << "status" << static_cast<int>(status);

    entry.state = ServiceState::Crashed;
    emit serviceCrashed(key, exitCode);
    emit serviceStateChanged(key, entry.state);
    scheduleRestart(entry);
}

void ServiceSupervisor::onProcessError(const QString& key, QProcess::ProcessError error)
{
    if (!m_entries.contains(key)) return;
    auto& entry = m_entries[key];

    if (entry.state == ServiceState::Stopped) return;

    qWarning() << "[ServiceSupervisor] Process error for" << key << "error:" << error;
    entry.state = ServiceState::Crashed;
    emit serviceCrashed(key, -1);
    emit serviceStateChanged(key, entry.state);
    scheduleRestart(entry);
}

} // namespace forge::core
