#include "forge/core/ProjectModel.h"
#include <QDir>
#include <QDebug>

namespace forge::core {

ProjectModel::ProjectModel(QObject* parent)
    : QObject(parent)
{}

bool ProjectModel::openFolder(const QString& folderPath)
{
    const QDir dir(folderPath);
    if (!dir.exists()) {
        qWarning() << "[ProjectModel] Folder does not exist:" << folderPath;
        return false;
    }

    m_rootPath = dir.absolutePath();
    m_isOpen   = true;
    emit projectOpened(m_rootPath);
    qInfo() << "[ProjectModel] Opened project:" << m_rootPath;
    return true;
}

void ProjectModel::close()
{
    if (!m_isOpen) return;
    m_isOpen   = false;
    m_rootPath.clear();
    emit projectClosed();
}

bool    ProjectModel::isOpen()      const { return m_isOpen; }
QString ProjectModel::rootPath()    const { return m_rootPath; }
QString ProjectModel::displayName() const {
    return m_isOpen ? QDir(m_rootPath).dirName() : QStringLiteral("No Project");
}

} // namespace forge::core
