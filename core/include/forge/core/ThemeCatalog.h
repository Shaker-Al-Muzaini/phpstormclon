#pragma once
#include <QJsonObject>
#include <QMap>
#include <QString>

namespace forge::core {
class ThemeCatalog final {
public:
    static QMap<QString, QString> themes();
    static QJsonObject load(const QString& name);
    static QJsonObject readJson(const QString& path, QString* error = nullptr);
    static QJsonObject convertVsCode(const QJsonObject& source);
    static QByteArray stripJsonComments(const QByteArray& input);
};
}  // namespace forge::core
