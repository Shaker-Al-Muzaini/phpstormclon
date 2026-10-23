#include "forge/core/ActionManager.h"

#include <QDebug>

namespace forge::core {

ActionManager::ActionManager(QObject* parent) : QObject(parent) {}

QAction* ActionManager::registerAction(const QString& id, const QString& displayName,
                                       std::function<void()> handler) {
    if (m_actions.contains(id)) {
        qWarning() << "[ActionManager] Action already registered:" << id;
        return m_actions.value(id);
    }

    auto* action = new QAction(displayName, this);
    action->setObjectName(id);
    connect(action, &QAction::triggered, this, [h = std::move(handler)]() { h(); });
    m_actions.insert(id, action);
    return action;
}

QAction* ActionManager::action(const QString& id) const {
    return m_actions.value(id, nullptr);
}

void ActionManager::applyKeymap(const QHash<QString, QString>& bindings) {
    for (auto it = bindings.cbegin(); it != bindings.cend(); ++it) {
        QAction* act = m_actions.value(it.key(), nullptr);
        if (!act) continue;
        act->setShortcut(QKeySequence(it.value()));
    }
}

QStringList ActionManager::allActionIds() const {
    return m_actions.keys();
}

}  // namespace forge::core
