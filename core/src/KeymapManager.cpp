#include "forge/core/KeymapManager.h"
#include "forge/core/ActionManager.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

namespace forge::core {

KeymapManager::KeymapManager(ActionManager* actionManager, QObject* parent)
    : QObject(parent)
    , m_actionManager(actionManager)
{
    Q_ASSERT(actionManager);
}

bool KeymapManager::loadDefaultKeymap()
{
    return loadKeymap(QStringLiteral(":/keymaps/default.json"));
}

bool KeymapManager::loadKeymap(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[KeymapManager] Cannot open keymap file:" << filePath;
        return false;
    }

    QJsonParseError err;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "[KeymapManager] JSON parse error in" << filePath << ":" << err.errorString();
        return false;
    }

    const QJsonObject root = doc.object();
    const QHash<QString, QString> bindings = parseBindings(root);

    if (!applyBindings(bindings)) {
        return false;
    }

    m_activeKeymapName = root.value(QStringLiteral("name")).toString(QStringLiteral("Unknown"));
    emit keymapChanged(m_activeKeymapName);
    qInfo() << "[KeymapManager] Loaded keymap:" << m_activeKeymapName << "from" << filePath;
    return true;
}

QString KeymapManager::activeKeymapName() const
{
    return m_activeKeymapName;
}

QHash<QString, QString> KeymapManager::parseBindings(const QJsonObject& root) const
{
    QHash<QString, QString> bindings;
    const QJsonObject b = root.value(QStringLiteral("bindings")).toObject();
    for (auto it = b.constBegin(); it != b.constEnd(); ++it) {
        bindings.insert(it.key(), it.value().toString());
    }
    return bindings;
}

bool KeymapManager::applyBindings(const QHash<QString, QString>& bindings)
{
    m_actionManager->applyKeymap(bindings);
    return true;
}

} // namespace forge::core
