#include "forge/core/ServiceSupervisor.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace forge::core {
namespace {

QByteArray shutdownNotification() {
    const QByteArray body = QByteArrayLiteral("{\"jsonrpc\":\"2.0\",\"method\":\"$/shutdown\"}");
    return QByteArrayLiteral("Content-Length: ") + QByteArray::number(body.size()) +
           QByteArrayLiteral("\r\n\r\n") + body;
}

}  // namespace

ServiceSupervisor::ServiceSupervisor(QObject* parent) : QObject(parent) {}

ServiceSupervisor::~ServiceSupervisor() {
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        Entry& entry = it.value();
        if (entry.restartTimer) {
            entry.restartTimer->stop();
        }
        if (entry.healthyTimer) {
            entry.healthyTimer->stop();
        }
        if (entry.terminateTimer) {
            entry.terminateTimer->stop();
        }
        if (entry.killTimer) {
            entry.killTimer->stop();
        }
        if (entry.process && entry.process->state() != QProcess::NotRunning) {
            entry.process->kill();
        }
    }
}

void ServiceSupervisor::registerService(const ServiceConfig& cfg) {
    if (m_entries.contains(cfg.key)) {
        qWarning() << "[ServiceSupervisor] Service already registered:" << cfg.key;
        return;
    }

    Entry entry;
    entry.config = cfg;
    entry.currentBackMs = cfg.backoffMs;
    m_entries.insert(cfg.key, entry);
    startProcess(m_entries[cfg.key]);
}

void ServiceSupervisor::stopService(const QString& key) {
    auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        return;
    }
    Entry& entry = it.value();
    entry.state = ServiceState::Stopped;
    emit serviceStateChanged(key, entry.state);

    if (entry.restartTimer) {
        entry.restartTimer->stop();
    }
    if (entry.healthyTimer) {
        entry.healthyTimer->stop();
    }
    if (entry.process && entry.process->state() != QProcess::NotRunning) {
        entry.process->write(shutdownNotification());
        entry.process->closeWriteChannel();

        if (!entry.terminateTimer) {
            entry.terminateTimer = new QTimer(this);
            entry.terminateTimer->setSingleShot(true);
            connect(entry.terminateTimer, &QTimer::timeout, this, [this, key]() {
                auto service = m_entries.find(key);
                if (service == m_entries.end() || service->state != ServiceState::Stopped ||
                    !service->process || service->process->state() == QProcess::NotRunning) {
                    return;
                }
                service->process->terminate();
                service->killTimer->start(1000);
            });
            entry.killTimer = new QTimer(this);
            entry.killTimer->setSingleShot(true);
            connect(entry.killTimer, &QTimer::timeout, this, [this, key]() {
                auto service = m_entries.find(key);
                if (service != m_entries.end() && service->state == ServiceState::Stopped &&
                    service->process && service->process->state() != QProcess::NotRunning) {
                    service->process->kill();
                }
            });
        }
        entry.terminateTimer->start(3000);
    } else {
        finishStop(entry);
    }
    emitAllStoppedIfReady();
}

void ServiceSupervisor::stopAll() {
    const QStringList keys = m_entries.keys();
    for (const QString& key : keys) {
        stopService(key);
    }
    emitAllStoppedIfReady();
}

ServiceSupervisor::ServiceState ServiceSupervisor::state(const QString& key) const {
    if (!m_entries.contains(key)) {
        return ServiceState::Stopped;
    }
    return m_entries.value(key).state;
}

QProcess* ServiceSupervisor::process(const QString& key) const {
    const auto it = m_entries.constFind(key);
    return it == m_entries.cend() ? nullptr : it->process;
}

bool ServiceSupervisor::allStopped() const {
    for (const Entry& entry : m_entries) {
        if (entry.state != ServiceState::Stopped ||
            (entry.process && entry.process->state() != QProcess::NotRunning)) {
            return false;
        }
    }
    return true;
}

void ServiceSupervisor::startProcess(Entry& entry) {
    if (!entry.process) {
        entry.process = new QProcess(this);
        const QString key = entry.config.key;
        const QString logDirectory =
            QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                .filePath(QStringLiteral("logs"));
        if (!QDir().mkpath(logDirectory)) {
            qWarning() << "[ServiceSupervisor] Could not create service log directory:"
                       << logDirectory;
        } else {
            entry.stderrLog = new QFile(
                QDir(logDirectory).filePath(QStringLiteral("%1-stderr.log").arg(key)), this);
            if (!entry.stderrLog->open(QIODevice::WriteOnly | QIODevice::Append)) {
                qWarning() << "[ServiceSupervisor] Could not open stderr log for" << key << ":"
                           << entry.stderrLog->errorString();
                delete entry.stderrLog;
                entry.stderrLog = nullptr;
            }
        }

        connect(entry.process, &QProcess::started, this, [this, key]() {
            auto service = m_entries.find(key);
            if (service == m_entries.end() || service->state == ServiceState::Stopped) {
                return;
            }
            service->state = ServiceState::Running;
            emit serviceStarted(key);
            emit serviceStateChanged(key, service->state);
            if (!service->healthyTimer) {
                service->healthyTimer = new QTimer(this);
                service->healthyTimer->setSingleShot(true);
                connect(service->healthyTimer, &QTimer::timeout, this, [this, key]() {
                    auto healthyService = m_entries.find(key);
                    if (healthyService != m_entries.end() &&
                        healthyService->state == ServiceState::Running) {
                        healthyService->restartCount = 0;
                        healthyService->currentBackMs = healthyService->config.backoffMs;
                        qInfo() << "[ServiceSupervisor] Reset restart backoff after healthy uptime:"
                                << key;
                    }
                });
            }
            service->healthyTimer->start(qMax(0, service->config.healthyUptimeMs));
            qInfo() << "[ServiceSupervisor] Service started:" << key
                    << "PID:" << service->process->processId();
        });
        connect(entry.process, &QProcess::readyReadStandardError, this, [this, key]() {
            auto service = m_entries.find(key);
            if (service == m_entries.end() || !service->process) {
                return;
            }
            const QByteArray stderrBytes = service->process->readAllStandardError();
            if (!stderrBytes.isEmpty()) {
                qWarning().noquote()
                    << QStringLiteral("[%1 stderr] %2")
                           .arg(key, QString::fromLocal8Bit(stderrBytes).trimmed());
                if (service->stderrLog) {
                    if (service->stderrLog->write(stderrBytes) != stderrBytes.size()) {
                        qWarning() << "[ServiceSupervisor] Could not write stderr log for" << key
                                   << ":" << service->stderrLog->errorString();
                    }
                    service->stderrLog->flush();
                }
            }
        });
        connect(entry.process, &QProcess::finished, this,
                [this, key](int exitCode, QProcess::ExitStatus status) {
                    onProcessFinished(key, exitCode, status);
                });
        connect(entry.process, &QProcess::errorOccurred, this,
                [this, key](QProcess::ProcessError error) { onProcessError(key, error); });
    }

    if (entry.terminateTimer) {
        entry.terminateTimer->stop();
    }
    if (entry.killTimer) {
        entry.killTimer->stop();
    }
    entry.state = ServiceState::Starting;
    emit serviceStateChanged(entry.config.key, entry.state);
    entry.process->setProcessChannelMode(QProcess::SeparateChannels);
    entry.process->setProgram(entry.config.executable);
    entry.process->setArguments(entry.config.arguments);
    entry.process->start();
}

void ServiceSupervisor::scheduleRestart(Entry& entry) {
    if (entry.state == ServiceState::Stopped) {
        finishStop(entry);
        return;
    }
    if (entry.restartTimer && entry.restartTimer->isActive()) {
        return;
    }
    if (entry.restartCount >= entry.config.maxRestarts) {
        entry.state = ServiceState::Disabled;
        const QString reason =
            QStringLiteral("Exceeded max restarts (%1)").arg(entry.config.maxRestarts);
        emit serviceDisabled(entry.config.key, reason);
        emit serviceStateChanged(entry.config.key, entry.state);
        qCritical() << "[ServiceSupervisor] Service disabled:" << entry.config.key << reason;
        return;
    }

    ++entry.restartCount;
    const int delay = entry.currentBackMs;
    entry.currentBackMs = qMin(entry.currentBackMs * 2, 30000);
    qWarning() << "[ServiceSupervisor] Restarting" << entry.config.key << "in" << delay
               << "ms (attempt" << entry.restartCount << ")";

    if (!entry.restartTimer) {
        entry.restartTimer = new QTimer(this);
        entry.restartTimer->setSingleShot(true);
        const QString key = entry.config.key;
        connect(entry.restartTimer, &QTimer::timeout, this, [this, key]() {
            auto service = m_entries.find(key);
            if (service != m_entries.end() && service->state != ServiceState::Stopped) {
                startProcess(service.value());
            }
        });
    }
    entry.restartTimer->start(delay);
}

void ServiceSupervisor::onProcessFinished(const QString& key, int exitCode,
                                          QProcess::ExitStatus status) {
    auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        return;
    }
    Entry& entry = it.value();
    if (entry.healthyTimer) {
        entry.healthyTimer->stop();
    }
    if (entry.terminateTimer) {
        entry.terminateTimer->stop();
    }
    if (entry.killTimer) {
        entry.killTimer->stop();
    }
    if (entry.state == ServiceState::Stopped) {
        finishStop(entry);
        return;
    }

    qWarning() << "[ServiceSupervisor] Service" << key << "exited with code" << exitCode << "status"
               << static_cast<int>(status);
    entry.state = ServiceState::Crashed;
    emit serviceCrashed(key, exitCode);
    emit serviceStateChanged(key, entry.state);
    scheduleRestart(entry);
}

void ServiceSupervisor::onProcessError(const QString& key, QProcess::ProcessError error) {
    auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        return;
    }
    Entry& entry = it.value();
    if (entry.state == ServiceState::Stopped) {
        finishStop(entry);
        return;
    }
    if (entry.state == ServiceState::Crashed || entry.state == ServiceState::Disabled) {
        return;
    }

    qWarning() << "[ServiceSupervisor] Process error for" << key << "error:" << error
               << entry.process->errorString();
    entry.state = ServiceState::Crashed;
    emit serviceCrashed(key, -1);
    emit serviceStateChanged(key, entry.state);
    scheduleRestart(entry);
}

void ServiceSupervisor::finishStop(Entry& entry) {
    entry.state = ServiceState::Stopped;
    emit serviceStateChanged(entry.config.key, entry.state);
    emitAllStoppedIfReady();
}

void ServiceSupervisor::emitAllStoppedIfReady() {
    if (allStopped()) {
        emit allServicesStopped();
    }
}

}  // namespace forge::core
