#pragma once
#include <QObject>
#include <QVariant>
#include <QString>
#include <QJsonObject>

namespace forge::core {

/**
 * @brief IDE settings storage with per-project overrides.
 *
 * Settings are stored as JSON under %LOCALAPPDATA%/ForgeIDE/settings.json
 * and optionally overridden by .forge/settings.json in the project root.
 */
class Settings : public QObject {
    Q_OBJECT
public:
    explicit Settings(QObject* parent = nullptr);

    static Settings* instance();

    QVariant value(const QString& key, const QVariant& defaultValue = {}) const;
    void     setValue(const QString& key, const QVariant& value);
    void     save();
    void     load();

signals:
    void settingChanged(const QString& key, const QVariant& newValue);

private:
    QJsonObject m_global;
    QJsonObject m_project;

    static Settings* s_instance;
};

} // namespace forge::core
