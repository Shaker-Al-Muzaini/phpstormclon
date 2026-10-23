#include "forge/core/Settings.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

namespace forge::core {

Settings* Settings::s_instance = nullptr;

Settings::Settings(QObject* parent) : QObject(parent) {
    Q_ASSERT(!s_instance);
    s_instance = this;
    load();
}

Settings::~Settings() {
    s_instance = nullptr;
}

Settings* Settings::instance() {
    return s_instance;
}

QVariant Settings::value(const QString& key, const QVariant& defaultValue) const {
    if (m_project.contains(key)) {
        return m_project.value(key).toVariant();
    }
    if (m_global.contains(key)) {
        return m_global.value(key).toVariant();
    }
    return defaultValue;
}

void Settings::setValue(const QString& key, const QVariant& value) {
    m_global[key] = QJsonValue::fromVariant(value);
    emit settingChanged(key, value);
}

void Settings::save() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/settings.json");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "[Settings] Cannot write settings to" << path;
        return;
    }
    const auto bytes = QJsonDocument(m_global).toJson();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        qWarning() << "[Settings] Could not save settings atomically:" << file.errorString();
        return;
    }
    qDebug() << "[Settings] Saved to" << path;
}

void Settings::load() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString path = dir + QStringLiteral("/settings.json");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "[Settings] No existing settings file, using defaults";
        return;
    }
    QJsonParseError err;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "[Settings] Parse error:" << err.errorString();
        return;
    }
    m_global = doc.object();
    qDebug() << "[Settings] Loaded from" << path;
}

}  // namespace forge::core
