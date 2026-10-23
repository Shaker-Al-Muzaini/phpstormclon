#pragma once
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace forge::core {

class ActionManager;

/**
 * @brief Loads and applies keymap JSON files.
 *
 * A keymap JSON has:
 *   { "$schema": "forge://keymap/v1",
 *     "name": "...",
 *     "extends": "default",   // optional
 *     "bindings": { "action.id": "Ctrl+S", ... } }
 *
 * The 'extends' field allows a keymap to inherit all bindings from another
 * and override only specific ones.
 */
class KeymapManager : public QObject {
    Q_OBJECT
public:
    explicit KeymapManager(ActionManager* actionManager, QObject* parent = nullptr);

    /// Load a keymap from an absolute or resource path and apply it.
    bool loadKeymap(const QString& filePath);

    /// Load the built-in default keymap from resources.
    bool loadDefaultKeymap();

    /// Return the name of the currently active keymap.
    QString activeKeymapName() const;

signals:
    void keymapChanged(const QString& name);

private:
    QHash<QString, QString> parseBindings(const QJsonObject& root) const;
    bool applyBindings(const QHash<QString, QString>& bindings);

    ActionManager* m_actionManager;
    QString m_activeKeymapName;
};

}  // namespace forge::core
