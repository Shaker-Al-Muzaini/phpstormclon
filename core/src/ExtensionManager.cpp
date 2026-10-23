#include "forge/core/ExtensionManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "forge/core/PackageArchive.h"
#include "forge/core/ThemeCatalog.h"

namespace forge::core {
namespace {
bool validId(const QString& id) {
    return QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_.-]{0,159}$").match(id).hasMatch() &&
           !id.contains("..");
}
bool writeJson(const QString& path, const QJsonDocument& document) {
    QSaveFile file(path);
    const auto bytes = document.toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QString confinedPath(const QString& root, const QString& relative) {
    if (relative.contains('\\') || relative.contains(':') || QDir::isAbsolutePath(relative) ||
        relative.split('/').contains(".."))
        return {};
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    const QString canonical = QFileInfo(QDir(root).filePath(relative)).canonicalFilePath();
    return canonical.startsWith(canonicalRoot + '/') ? canonical : QString();
}
}  // namespace

ExtensionManager::ExtensionManager(QString directory, QObject* parent) : QObject(parent) {
    m_directory =
        directory.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/plugins"
            : directory;
    QDir().mkpath(m_directory);
}
QJsonArray ExtensionManager::installed() const {
    QJsonArray result;
    for (const auto& id : QDir(m_directory).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!validId(id)) continue;
        auto metadata =
            ThemeCatalog::readJson(QDir(m_directory).filePath(id + "/forge-extension.json"));
        if (!metadata.isEmpty()) result.append(metadata);
    }
    return result;
}
bool ExtensionManager::installArchive(const QByteArray& bytes, QString* error) {
    QTemporaryDir extracted;
    if (!extracted.isValid() || !PackageArchive::extract(bytes, extracted.path(), error))
        return false;
    QString root = extracted.path();
    if (QFileInfo::exists(root + "/extension/package.json")) root += "/extension";
    return installFolder(root, error);
}
bool ExtensionManager::installFolder(const QString& folder, QString* error) {
    auto fail = [error](const QString& text) {
        if (error) *error = text;
        return false;
    };
    const bool vscode = QFileInfo::exists(QDir(folder).filePath("package.json"));
    const auto source = ThemeCatalog::readJson(
        QDir(folder).filePath(vscode ? "package.json" : "plugin.json"), error);
    if (source.isEmpty()) return false;
    if (source.contains("main") || source.contains("browser") || source.contains("entryLibrary"))
        return fail(
            "This extension requires an executable extension host. Forge currently installs "
            "declarative themes, snippets and keymaps only.");
    const QString id =
        vscode ? source.value("publisher").toString() + '.' + source.value("name").toString()
               : source.value("id").toString();
    const QString version = source.value("version").toString();
    if (!validId(id) ||
        !QRegularExpression("^\\d+\\.\\d+\\.\\d+(?:[-+].*)?$").match(version).hasMatch())
        return fail("Invalid extension identifier or semantic version.");
    QTemporaryDir stage(m_directory + "/.install-XXXXXX");
    if (!stage.isValid()) return fail("Cannot create extension staging directory.");
    const auto contributes = source.value("contributes").toObject();
    QJsonArray themes;
    int index = 0;
    for (const auto& contribution : contributes.value("themes").toArray()) {
        const auto item = contribution.toObject();
        QString relative = item.value("path").toString();
        if (relative.isEmpty()) relative = item.value("file").toString();
        const QString path = confinedPath(folder, relative);
        if (path.isEmpty()) return fail("Theme references a missing or unsafe path.");
        auto raw = ThemeCatalog::readJson(path, error);
        if (raw.isEmpty()) return false;
        if (raw.contains("include"))
            return fail("This theme uses an external include; supply a self-contained theme JSON.");
        raw["name"] = item.value("label").toString(
            raw.value("name").toString(source.value("displayName").toString(id)));
        const QString file = QStringLiteral("theme-%1.json").arg(index++);
        if (!writeJson(stage.filePath(file), QJsonDocument(ThemeCatalog::convertVsCode(raw))))
            return fail("Cannot save extension theme.");
        themes.append(file);
    }
    QJsonArray snippets;
    for (const auto& contribution : contributes.value("snippets").toArray()) {
        const auto item = contribution.toObject();
        const QString path = confinedPath(folder, item.value("path").toString());
        if (path.isEmpty()) return fail("Snippet file is missing or outside the package.");
        const auto definitions = ThemeCatalog::readJson(path, error);
        if (definitions.isEmpty()) return false;
        for (auto it = definitions.begin(); it != definitions.end(); ++it) {
            auto snippet = it.value().toObject();
            snippet["name"] = it.key();
            snippet["language"] = item.value("language");
            snippets.append(snippet);
        }
    }
    const QMap<QString, QString> commands{
        {"actions.find", "forge.navigate.find"},
        {"editor.action.startFindReplaceAction", "forge.navigate.localReplace"},
        {"workbench.action.quickOpen", "forge.navigate.goToFile"},
        {"workbench.action.showCommands", "forge.navigate.searchEverywhere"},
        {"workbench.action.files.save", "forge.file.save"},
        {"workbench.action.files.saveAll", "forge.file.saveAll"},
        {"undo", "forge.edit.undo"},
        {"redo", "forge.edit.redo"},
        {"editor.action.commentLine", "forge.edit.toggleComment"}};
    QJsonObject bindings;
    const auto bindingsValue = contributes.value("keybindings");
    if (bindingsValue.isObject())
        bindings = bindingsValue.toObject();
    else
        for (const auto& value : bindingsValue.toArray()) {
            const auto item = value.toObject();
            const QString command = item.value("command").toString();
            if (!commands.contains(command) || item.contains("when")) continue;
            QString key = item.value("win").toString(item.value("key").toString());
            if (key.contains(' ')) continue;
            key.replace("ctrl", "Ctrl", Qt::CaseInsensitive);
            bindings[commands.value(command)] = key;
        }
    if (themes.isEmpty() && snippets.isEmpty() && bindings.isEmpty())
        return fail("No supported declarative contributions were found in this package.");
    QJsonObject manifest{
        {"id", id},
        {"name", source.value("displayName").toString(source.value("name").toString(id))},
        {"version", version},
        {"license", source.value("license").toString("See package license")},
        {"enabled", true},
        {"themeFiles", themes},
        {"snippetCount", snippets.size()},
        {"keybindingCount", bindings.size()}};
    if (!writeJson(stage.filePath("forge-extension.json"), QJsonDocument(manifest)) ||
        !writeJson(stage.filePath("snippets.json"), QJsonDocument(snippets)) ||
        !writeJson(stage.filePath("keymap.json"), QJsonDocument(bindings)))
        return fail("Cannot write extension state.");
    for (const auto& file :
         QDir(folder).entryList({"LICENSE*", "license*", "NOTICE*"}, QDir::Files)) {
        const QString path = confinedPath(folder, file);
        if (path.isEmpty() || !QFile::copy(path, stage.filePath(file)))
            return fail("Cannot retain package license.");
    }
    const QString target = QDir(m_directory).filePath(id);
    const QString backup = target + ".previous";
    if (QFileInfo::exists(backup))
        return fail("A previous install recovery directory exists; resolve it before updating.");
    const bool existing = QFileInfo::exists(target);
    if (existing && !QDir().rename(target, backup))
        return fail("Cannot stage the previous extension version.");
    if (!QDir().rename(stage.path(), target)) {
        if (existing) QDir().rename(backup, target);
        return fail("Cannot activate the extension.");
    }
    stage.setAutoRemove(false);
    if (existing) QDir(backup).removeRecursively();
    emit changed();
    return true;
}
bool ExtensionManager::setEnabled(const QString& id, bool enabled, QString* error) {
    if (!validId(id)) return false;
    const QString path = QDir(m_directory).filePath(id + "/forge-extension.json");
    auto manifest = ThemeCatalog::readJson(path, error);
    if (manifest.isEmpty()) return false;
    manifest["enabled"] = enabled;
    if (!writeJson(path, QJsonDocument(manifest))) {
        if (error) *error = "Cannot save extension state.";
        return false;
    }
    emit changed();
    return true;
}
bool ExtensionManager::uninstall(const QString& id, QString* error) {
    if (!validId(id) ||
        !QFileInfo::exists(QDir(m_directory).filePath(id + "/forge-extension.json")))
        return false;
    if (!QDir(QDir(m_directory).filePath(id)).removeRecursively()) {
        if (error) *error = "Cannot remove extension files.";
        return false;
    }
    emit changed();
    return true;
}
QJsonArray ExtensionManager::snippets() const {
    QJsonArray result;
    for (const auto& value : installed()) {
        const auto manifest = value.toObject();
        if (!manifest.value("enabled").toBool()) continue;
        QFile file(QDir(m_directory).filePath(manifest.value("id").toString() + "/snippets.json"));
        if (!file.open(QIODevice::ReadOnly)) continue;
        for (const auto& snippet : QJsonDocument::fromJson(file.readAll()).array())
            result.append(snippet);
    }
    return result;
}
QJsonObject ExtensionManager::keybindings() const {
    QJsonObject result;
    for (const auto& value : installed()) {
        const auto manifest = value.toObject();
        if (!manifest.value("enabled").toBool()) continue;
        const auto bindings = ThemeCatalog::readJson(
            QDir(m_directory).filePath(manifest.value("id").toString() + "/keymap.json"));
        for (auto it = bindings.begin(); it != bindings.end(); ++it) result[it.key()] = it.value();
    }
    return result;
}
}  // namespace forge::core
