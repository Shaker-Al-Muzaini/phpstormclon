#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace forge::core {
class ExtensionManager final : public QObject {
    Q_OBJECT
public:
    explicit ExtensionManager(QString directory = {}, QObject* parent = nullptr);
    QString directory() const {
        return m_directory;
    }
    QJsonArray installed() const;
    bool installArchive(const QByteArray& bytes, QString* error);
    bool installFolder(const QString& folder, QString* error);
    bool setEnabled(const QString& id, bool enabled, QString* error);
    bool uninstall(const QString& id, QString* error);
    QJsonArray snippets() const;
    QJsonObject keybindings() const;
signals:
    void changed();

private:
    QString m_directory;
};
}  // namespace forge::core
