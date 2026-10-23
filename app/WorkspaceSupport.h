#pragma once
#include "WorkspaceIncludes.h"

QIcon forgeIcon(const QString& name, const QColor& foreground, const QColor& accent);
QString themeResourcePath(const QString& name);
QString themedColor(const QJsonObject& colors, const QString& key, const QString& fallback);
QString fileLanguageName(const QFileInfo& info);
void collectProjectFiles(const QString& directory, QStringList& files, int depth = 0);
class ForgeFileSystemModel final : public QFileSystemModel {
public:
    using QFileSystemModel::QFileSystemModel;

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (role == Qt::ToolTipRole && index.isValid()) {
            const QFileInfo info(filePath(index));
            if (info.isDir()) {
                return info.absoluteFilePath();
            }
            return QStringLiteral("%1\nLanguage: %2\n%3")
                .arg(info.fileName(), fileLanguageName(info), info.absoluteFilePath());
        }
        return QFileSystemModel::data(index, role);
    }
};

class ForgeFileIconProvider final : public QFileIconProvider {
public:
    ForgeFileIconProvider(QColor foreground, QColor accent)
        : m_foreground(std::move(foreground)), m_accent(std::move(accent)) {
        primeIcons();
    }

    void setColors(QColor foreground, QColor accent) {
        QMutexLocker locker(&m_mutex);
        m_foreground = std::move(foreground);
        m_accent = std::move(accent);
        m_icons.clear();
        primeIcons();
    }

    QIcon icon(const QFileInfo& info) const override {
        QMutexLocker locker(&m_mutex);
        if (info.isDir()) {
            return cachedIcon(QStringLiteral("folder"), QStringLiteral("folder"), m_accent);
        }

        const QString fileName = info.fileName().toLower();
        QString badge;
        QColor badgeColor = m_accent;
        if (fileName.endsWith(QStringLiteral(".blade.php"))) {
            badge = QStringLiteral("BLD");
            badgeColor = QColor(QStringLiteral("#A98BFF"));
        } else if (fileName.endsWith(QStringLiteral(".php"))) {
            badge = QStringLiteral("PHP");
            badgeColor = QColor(QStringLiteral("#A98BFF"));
        } else if (fileName.endsWith(QStringLiteral(".tsx")) ||
                   fileName.endsWith(QStringLiteral(".ts"))) {
            badge = QStringLiteral("TS");
            badgeColor = QColor(QStringLiteral("#62A7E8"));
        } else if (fileName.endsWith(QStringLiteral(".jsx")) ||
                   fileName.endsWith(QStringLiteral(".js"))) {
            badge = QStringLiteral("JS");
            badgeColor = QColor(QStringLiteral("#E6C64A"));
        } else if (fileName.endsWith(QStringLiteral(".json"))) {
            badge = QStringLiteral("{ }");
            badgeColor = QColor(QStringLiteral("#71B8D0"));
        } else if (fileName.endsWith(QStringLiteral(".css")) ||
                   fileName.endsWith(QStringLiteral(".scss"))) {
            badge = QStringLiteral("CSS");
            badgeColor = QColor(QStringLiteral("#D789C8"));
        } else if (fileName.endsWith(QStringLiteral(".html")) ||
                   fileName.endsWith(QStringLiteral(".vue"))) {
            badge = QStringLiteral("HTML");
            badgeColor = QColor(QStringLiteral("#E77B56"));
        } else if (fileName.endsWith(QStringLiteral(".yml")) ||
                   fileName.endsWith(QStringLiteral(".yaml"))) {
            badge = QStringLiteral("YML");
            badgeColor = QColor(QStringLiteral("#D2798B"));
        } else if (fileName.endsWith(QStringLiteral(".md"))) {
            badge = QStringLiteral("MD");
            badgeColor = QColor(QStringLiteral("#7DA6C6"));
        } else if (fileName.endsWith(QStringLiteral(".sql"))) {
            badge = QStringLiteral("SQL");
            badgeColor = QColor(QStringLiteral("#66B7A7"));
        } else if (fileName.endsWith(QStringLiteral(".py"))) {
            badge = QStringLiteral("PY");
            badgeColor = QColor(QStringLiteral("#E6C64A"));
        } else if (fileName == QStringLiteral(".env") ||
                   fileName.startsWith(QStringLiteral(".env."))) {
            badge = QStringLiteral("ENV");
            badgeColor = QColor(QStringLiteral("#83BB76"));
        } else if (fileName == QStringLiteral("dockerfile") ||
                   fileName.startsWith(QStringLiteral("dockerfile."))) {
            badge = QStringLiteral("DKR");
            badgeColor = QColor(QStringLiteral("#64A9D7"));
        }

        const QString iconName =
            badge.isEmpty() ? QStringLiteral("file") : QStringLiteral("code:") + badge;
        return cachedIcon(iconName, iconName, badgeColor);
    }

private:
    void primeIcons() const {
        // QFileSystemModel requests icons on a worker thread, so create pixmaps here first.
        cachedIcon(QStringLiteral("folder"), QStringLiteral("folder"), m_accent);
        cachedIcon(QStringLiteral("file"), QStringLiteral("file"), m_accent);

        const std::array<std::pair<QString, QColor>, 13> badges{{
            {QStringLiteral("BLD"), QColor(QStringLiteral("#A98BFF"))},
            {QStringLiteral("PHP"), QColor(QStringLiteral("#A98BFF"))},
            {QStringLiteral("TS"), QColor(QStringLiteral("#62A7E8"))},
            {QStringLiteral("JS"), QColor(QStringLiteral("#E6C64A"))},
            {QStringLiteral("{ }"), QColor(QStringLiteral("#71B8D0"))},
            {QStringLiteral("CSS"), QColor(QStringLiteral("#D789C8"))},
            {QStringLiteral("HTML"), QColor(QStringLiteral("#E77B56"))},
            {QStringLiteral("YML"), QColor(QStringLiteral("#D2798B"))},
            {QStringLiteral("MD"), QColor(QStringLiteral("#7DA6C6"))},
            {QStringLiteral("SQL"), QColor(QStringLiteral("#66B7A7"))},
            {QStringLiteral("ENV"), QColor(QStringLiteral("#83BB76"))},
            {QStringLiteral("DKR"), QColor(QStringLiteral("#64A9D7"))},
            {QStringLiteral("PY"), QColor(QStringLiteral("#E6C64A"))},
        }};
        for (const auto& [badge, color] : badges) {
            const QString iconName = QStringLiteral("code:") + badge;
            cachedIcon(iconName, iconName, color);
        }
    }

    QIcon cachedIcon(const QString& key, const QString& iconName, const QColor& accent) const {
        const auto existing = m_icons.constFind(key);
        if (existing != m_icons.cend()) {
            return existing.value();
        }

        const QIcon icon = forgeIcon(iconName, m_foreground, accent);
        m_icons.insert(key, icon);
        return icon;
    }

    QColor m_foreground;
    QColor m_accent;
    mutable QMutex m_mutex;
    mutable QHash<QString, QIcon> m_icons;
};
