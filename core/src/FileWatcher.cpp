#include "forge/core/FileWatcher.h"

namespace forge::core {

FileWatcher::FileWatcher(QObject* parent)
    : QObject(parent)
{}

void FileWatcher::watchDirectory(const QString&)
{}

void FileWatcher::unwatchAll()
{}

} // namespace forge::core
