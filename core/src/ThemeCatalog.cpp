#include "forge/core/ThemeCatalog.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>

namespace forge::core {
QByteArray ThemeCatalog::stripJsonComments(const QByteArray& input) {
    QByteArray result;
    bool quoted = false;
    bool escaped = false;
    for (qsizetype i = 0; i < input.size(); ++i) {
        const char c = input.at(i);
        if (quoted) {
            result += c;
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
        } else if (c == '"') {
            quoted = true;
            result += c;
        } else if (c == '/' && i + 1 < input.size() && input.at(i + 1) == '/') {
            while (i < input.size() && input.at(i) != '\n') ++i;
            result += '\n';
        } else if (c == '/' && i + 1 < input.size() && input.at(i + 1) == '*') {
            i += 2;
            while (i + 1 < input.size() && !(input.at(i) == '*' && input.at(i + 1) == '/')) ++i;
            ++i;
            result += ' ';
        } else
            result += c;
    }
    QByteArray cleaned;
    quoted = false;
    escaped = false;
    for (qsizetype i = 0; i < result.size(); ++i) {
        const char c = result.at(i);
        if (!quoted && c == ',') {
            qsizetype next = i + 1;
            while (next < result.size() && QByteArray(" \r\n\t").contains(result.at(next))) ++next;
            if (next < result.size() && (result.at(next) == '}' || result.at(next) == ']'))
                continue;
        }
        cleaned += c;
        if (quoted && escaped)
            escaped = false;
        else if (quoted && c == '\\')
            escaped = true;
        else if (c == '"')
            quoted = !quoted;
    }
    return cleaned;
}

QJsonObject ThemeCatalog::readJson(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
        if (error) *error = QStringLiteral("Cannot read JSON file (limit 4 MiB): %1").arg(path);
        return {};
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(stripJsonComments(file.readAll()), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("Invalid JSON in %1: %2").arg(path, parse.errorString());
        return {};
    }
    return document.object();
}

QMap<QString, QString> ThemeCatalog::themes() {
    QMap<QString, QString> result;
    for (const QString& file :
         QDir(QStringLiteral(":/themes")).entryList({"*.json"}, QDir::Files)) {
        const QString path = QStringLiteral(":/themes/") + file;
        const auto root = readJson(path);
        if (!root.isEmpty()) result.insert(root.value("name").toString(), path);
    }
    const QDir plugins(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                       "/plugins");
    for (const QString& id : plugins.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto manifest = readJson(plugins.filePath(id + "/forge-extension.json"));
        if (!manifest.value("enabled").toBool()) continue;
        for (const auto& file : manifest.value("themeFiles").toArray()) {
            const QString relative = file.toString();
            if (relative.contains("..") || QDir::isAbsolutePath(relative)) continue;
            const QString path = plugins.filePath(id + "/" + relative);
            const auto theme = readJson(path);
            const QString name = theme.value("name").toString();
            if (!name.isEmpty()) result.insert(name + " [" + id + "]", path);
        }
    }
    return result;
}

QJsonObject ThemeCatalog::load(const QString& name) {
    return readJson(themes().value(name, QStringLiteral(":/themes/graphite.json")));
}

QJsonObject ThemeCatalog::convertVsCode(const QJsonObject& source) {
    if (source.contains("ui") && source.contains("editor") && source.contains("syntax"))
        return source;
    const auto colors = source.value("colors").toObject();
    const QColor background(colors.value("editor.background").toString("#1E1F22"));
    const bool light = background.lightness() > 128;
    auto result = readJson(light ? ":/themes/daylight.json" : ":/themes/graphite.json");
    auto ui = result.value("ui").toObject();
    auto editor = result.value("editor").toObject();
    auto syntax = result.value("syntax").toObject();
    const QMap<QString, QString> uiMap{{"sideBar.background", "backgroundPanel"},
                                       {"titleBar.activeBackground", "backgroundToolbar"},
                                       {"foreground", "foreground"},
                                       {"descriptionForeground", "foregroundMuted"},
                                       {"focusBorder", "accent"},
                                       {"list.activeSelectionBackground", "backgroundSelection"},
                                       {"statusBar.background", "statusBar"},
                                       {"statusBar.foreground", "statusBarForeground"},
                                       {"panel.border", "border"}};
    const QMap<QString, QString> editorMap{{"editor.background", "background"},
                                           {"editor.foreground", "foreground"},
                                           {"editorLineNumber.foreground", "lineNumber"},
                                           {"editor.selectionBackground", "selection"},
                                           {"editor.lineHighlightBackground", "currentLine"},
                                           {"editorCursor.foreground", "cursor"}};
    for (auto it = uiMap.begin(); it != uiMap.end(); ++it)
        if (QColor(colors.value(it.key()).toString()).isValid())
            ui[it.value()] = colors.value(it.key());
    for (auto it = editorMap.begin(); it != editorMap.end(); ++it)
        if (QColor(colors.value(it.key()).toString()).isValid())
            editor[it.value()] = colors.value(it.key());
    editor["gutterBackground"] = editor.value("background");
    ui["background"] = editor.value("background");
    const QMap<QString, QString> scopes{{"keyword", "keyword"},
                                        {"comment", "comment"},
                                        {"string", "string"},
                                        {"constant.numeric", "number"},
                                        {"entity.name.function", "function"},
                                        {"entity.name.type", "type"},
                                        {"variable", "phpVariable"},
                                        {"entity.name.tag", "tag"},
                                        {"entity.other.attribute-name", "tagAttribute"}};
    for (const auto& rule : source.value("tokenColors").toArray()) {
        const auto object = rule.toObject();
        QStringList names;
        if (object.value("scope").isArray())
            for (const auto& scope : object.value("scope").toArray()) names << scope.toString();
        else
            names = object.value("scope").toString().split(',');
        const QString color = object.value("settings").toObject().value("foreground").toString();
        if (!QColor(color).isValid()) continue;
        for (const QString& name : names)
            for (auto it = scopes.begin(); it != scopes.end(); ++it)
                if (name.trimmed() == it.key() || name.trimmed().startsWith(it.key() + '.'))
                    syntax[it.value()] = color;
    }
    result["name"] = source.value("name").toString("Imported Theme");
    result["ui"] = ui;
    result["editor"] = editor;
    result["syntax"] = syntax;
    return result;
}
}  // namespace forge::core
