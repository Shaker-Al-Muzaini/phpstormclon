#pragma once
#include <QObject>
#include <QAction>
#include <QKeySequence>
#include <QString>
#include <QHash>
#include <functional>

namespace forge::core {

/**
 * @brief Central registry for all IDE actions.
 *
 * Every IDE feature is exposed as a named action (e.g. "forge.file.open").
 * Keymaps are applied here, so the keymap JSON is the sole place to change
 * key bindings. Actions are created once and reused everywhere (menu, toolbar,
 * keyboard shortcut).
 */
class ActionManager : public QObject {
    Q_OBJECT
public:
    explicit ActionManager(QObject* parent = nullptr);

    /**
     * @brief Register a new action.
     * @param id          Unique dot-separated identifier (e.g. "forge.file.new")
     * @param displayName Human-readable name shown in menus / search
     * @param handler     Callable invoked when the action is triggered
     * @return            The created QAction (owned by ActionManager)
     */
    QAction* registerAction(const QString& id,
                            const QString& displayName,
                            std::function<void()> handler);

    /// Retrieve an action by id (returns nullptr if not found).
    QAction* action(const QString& id) const;

    /// Apply a keymap (map from action id -> key sequence string).
    void applyKeymap(const QHash<QString, QString>& bindings);

    /// List all registered action ids.
    QStringList allActionIds() const;

private:
    QHash<QString, QAction*> m_actions; ///< id -> QAction
};

} // namespace forge::core
