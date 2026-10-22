#include "forge/platform/Platform.h"
#include <QSysInfo>
#include <QStandardPaths>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>

namespace forge::platform {

QString Platform::osName()
{
    return QStringLiteral("%1 %2")
           .arg(QSysInfo::prettyProductName(),
                QSysInfo::currentCpuArchitecture());
}

QString Platform::appDataDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

QString Platform::findExecutable(const QString& name, const QStringList& extraPaths)
{
    // QProcess::findExecutable searches PATH automatically
    QString found = QStandardPaths::findExecutable(name, extraPaths);
    if (found.isEmpty()) {
        found = QStandardPaths::findExecutable(name);
    }
    return found;
}

void Platform::openUrl(const QString& url)
{
    QDesktopServices::openUrl(QUrl(url));
}

} // namespace forge::platform
