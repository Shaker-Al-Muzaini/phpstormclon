#pragma once
#include <QDir>
#include <QObject>
#include <QString>
#include <QStringList>

namespace forge::core {

/**
 * @brief Represents the currently open project.
 *
 * M0/M1: simple folder-based project. Full multi-root workspaces in M2+.
 */
class ProjectModel : public QObject {
    Q_OBJECT
public:
    explicit ProjectModel(QObject* parent = nullptr);

    /// Open a folder as a project. Returns true on success.
    bool openFolder(const QString& folderPath);

    /// Close the current project.
    void close();

    bool isOpen() const;
    QString rootPath() const;
    QString displayName() const;

signals:
    void projectOpened(const QString& rootPath);
    void projectClosed();

private:
    QString m_rootPath;
    bool m_isOpen = false;
};

}  // namespace forge::core
