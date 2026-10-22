#include "forge/core/ExtensionRegistry.h"
#include <QDebug>

namespace forge::core {

ExtensionRegistry::ExtensionRegistry(QObject* parent)
    : QObject(parent)
{
    // Declare all standard extension points from spec §9
    const QStringList builtIn = {
        QStringLiteral("forge.language"),
        QStringLiteral("forge.completionContributor"),
        QStringLiteral("forge.annotator"),
        QStringLiteral("forge.inspection"),
        QStringLiteral("forge.intention"),
        QStringLiteral("forge.refactoring"),
        QStringLiteral("forge.toolWindow"),
        QStringLiteral("forge.action"),
        QStringLiteral("forge.runConfiguration"),
        QStringLiteral("forge.projectTemplate"),
        QStringLiteral("forge.settingsPage"),
        QStringLiteral("forge.fileType"),
        QStringLiteral("forge.theme"),
        QStringLiteral("forge.keymap"),
        QStringLiteral("forge.liveTemplate"),
        QStringLiteral("forge.vcsProvider"),
        QStringLiteral("forge.dbDriver"),
        QStringLiteral("forge.llmProvider"),
        QStringLiteral("forge.indexContributor"),
    };
    for (const auto& id : builtIn) {
        declarePoint(id);
    }
}

void ExtensionRegistry::declarePoint(const QString& pointId, const QString& description)
{
    if (!m_points.contains(pointId)) {
        m_points.insert(pointId, description);
        qDebug() << "[ExtensionRegistry] Declared point:" << pointId;
    }
}

int ExtensionRegistry::registerExtension(const QString& pointId, QObject* impl)
{
    if (!m_points.contains(pointId)) {
        qWarning() << "[ExtensionRegistry] Unknown extension point:" << pointId;
        return -1;
    }
    const int handle = m_nextHandle++;
    m_extensions.append({ handle, pointId, impl });
    qDebug() << "[ExtensionRegistry] Registered extension for" << pointId << "handle:" << handle;
    return handle;
}

void ExtensionRegistry::unregisterExtension(int handle)
{
    const auto it = std::find_if(m_extensions.begin(), m_extensions.end(),
                                  [handle](const Entry& e) { return e.handle == handle; });
    if (it != m_extensions.end()) {
        qDebug() << "[ExtensionRegistry] Unregistered handle:" << handle;
        m_extensions.erase(it);
    }
}

QList<QObject*> ExtensionRegistry::extensions(const QString& pointId) const
{
    QList<QObject*> result;
    for (const auto& e : m_extensions) {
        if (e.pointId == pointId) result.append(e.impl);
    }
    return result;
}

QStringList ExtensionRegistry::declaredPoints() const
{
    return m_points.keys();
}

} // namespace forge::core
