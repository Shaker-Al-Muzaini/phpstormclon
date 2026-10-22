#pragma once
#include <QObject>
#include <QString>
#include <QHash>
#include <QList>
#include <functional>
#include <memory>

namespace forge::core {

/**
 * @brief Central extension point registry for the plugin system.
 *
 * Extension points are declared by core and plugins.
 * Plugins register implementations against named extension points.
 * Consumers iterate registered extensions.
 *
 * M0/M1: registry and lookup only; full plugin loading in M8.
 */
class ExtensionRegistry : public QObject {
    Q_OBJECT
public:
    explicit ExtensionRegistry(QObject* parent = nullptr);

    /// Declare an extension point. Idempotent.
    void declarePoint(const QString& pointId, const QString& description = {});

    /// Register an implementation against an extension point.
    /// Returns a registration handle (pass to unregister).
    int registerExtension(const QString& pointId, QObject* impl);

    /// Unregister a previously registered extension.
    void unregisterExtension(int handle);

    /// Get all implementations registered for a point.
    QList<QObject*> extensions(const QString& pointId) const;

    /// Return all declared extension point IDs.
    QStringList declaredPoints() const;

private:
    struct Entry {
        int      handle;
        QString  pointId;
        QObject* impl;
    };

    QHash<QString, QString>  m_points;     // pointId -> description
    QList<Entry>             m_extensions;
    int                      m_nextHandle = 1;
};

} // namespace forge::core
