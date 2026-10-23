#pragma once
#include <QFileSystemWatcher>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

namespace forge::core {

/**
 * @brief Watches project files for external changes.
 *
 * M2+: uses QFileSystemWatcher with debouncing.
 * M0/M1: stub.
 */
class FileWatcher : public QObject {
    Q_OBJECT
public:
    explicit FileWatcher(QObject* parent = nullptr);
    void watchDirectory(const QString& path);
    void watchFile(const QString& path);
    void unwatchAll();
signals:
    void fileChanged(const QString& path);
    void directoryChanged(const QString& path);

private:
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QSet<QString> m_files;
    QSet<QString> m_pending;
};

}  // namespace forge::core
