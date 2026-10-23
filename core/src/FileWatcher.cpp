#include "forge/core/FileWatcher.h"

#include <QFileInfo>

namespace forge::core {

FileWatcher::FileWatcher(QObject* parent) : QObject(parent) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(150);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
        m_pending.insert(path);
        m_debounce.start();
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString& path) {
        emit directoryChanged(path);
        for (const auto& file : m_files)
            if (QFileInfo(file).absolutePath() == path) m_pending.insert(file);
        m_debounce.start();
    });
    connect(&m_debounce, &QTimer::timeout, this, [this]() {
        const auto pending = m_pending;
        m_pending.clear();
        for (const auto& path : pending) {
            if (QFileInfo::exists(path) && !m_watcher.files().contains(path))
                m_watcher.addPath(path);
            emit fileChanged(path);
        }
    });
}

void FileWatcher::watchDirectory(const QString& path) {
    if (!m_watcher.directories().contains(path)) m_watcher.addPath(path);
}
void FileWatcher::watchFile(const QString& path) {
    m_files.insert(path);
    watchDirectory(QFileInfo(path).absolutePath());
    if (!m_watcher.files().contains(path)) m_watcher.addPath(path);
}

void FileWatcher::unwatchAll() {
    if (!m_watcher.files().isEmpty()) m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
    m_files.clear();
    m_pending.clear();
    m_debounce.stop();
}

}  // namespace forge::core
