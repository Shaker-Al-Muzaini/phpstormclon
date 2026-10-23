#include "WorkspaceSupport.h"

#include "forge/core/ThemeCatalog.h"

QString themeResourcePath(const QString& name) {
    return forge::core::ThemeCatalog::themes().value(name,
                                                     QStringLiteral(":/themes/graphite.json"));
}

QString themedColor(const QJsonObject& colors, const QString& key, const QString& fallback) {
    const QString color = colors.value(key).toString(fallback);
    return QColor(color).isValid() ? color : fallback;
}

QString fileLanguageName(const QFileInfo& info) {
    const QString name = info.fileName().toLower();
    if (name.endsWith(QStringLiteral(".blade.php"))) return QStringLiteral("Blade template");
    if (name.endsWith(QStringLiteral(".php"))) return QStringLiteral("PHP");
    if (name.endsWith(QStringLiteral(".tsx"))) return QStringLiteral("TypeScript React");
    if (name.endsWith(QStringLiteral(".ts"))) return QStringLiteral("TypeScript");
    if (name.endsWith(QStringLiteral(".jsx"))) return QStringLiteral("JavaScript React");
    if (name.endsWith(QStringLiteral(".js"))) return QStringLiteral("JavaScript");
    if (name.endsWith(QStringLiteral(".vue"))) return QStringLiteral("Vue");
    if (name.endsWith(QStringLiteral(".json"))) return QStringLiteral("JSON");
    if (name.endsWith(QStringLiteral(".yaml")) || name.endsWith(QStringLiteral(".yml"))) {
        return QStringLiteral("YAML");
    }
    if (name.endsWith(QStringLiteral(".xml"))) return QStringLiteral("XML");
    if (name.endsWith(QStringLiteral(".md"))) return QStringLiteral("Markdown");
    if (name.endsWith(QStringLiteral(".py"))) return QStringLiteral("Python");
    if (name.endsWith(QStringLiteral(".sql"))) return QStringLiteral("SQL");
    if (name.endsWith(QStringLiteral(".html")) || name.endsWith(QStringLiteral(".htm"))) {
        return QStringLiteral("HTML");
    }
    if (name.endsWith(QStringLiteral(".scss"))) return QStringLiteral("SCSS");
    if (name.endsWith(QStringLiteral(".css"))) return QStringLiteral("CSS");
    if (name.endsWith(QStringLiteral(".env")) || name.startsWith(QStringLiteral(".env."))) {
        return QStringLiteral("Environment");
    }
    if (name == QStringLiteral("dockerfile") || name.startsWith(QStringLiteral("dockerfile."))) {
        return QStringLiteral("Dockerfile");
    }
    if (name == QStringLiteral("makefile")) return QStringLiteral("Makefile");
    return QStringLiteral("Text");
}

void collectProjectFiles(const QString& directory, QStringList& files, int depth) {
    if (files.size() >= 15000 || depth >= 40) {
        return;
    }

    const QDir current(directory);
    const QFileInfoList fileEntries =
        current.entryInfoList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& file : fileEntries) {
        files.append(file.absoluteFilePath());
        if (files.size() >= 15000) {
            return;
        }
    }

    static const QStringList excludedDirectories = {
        QStringLiteral(".git"), QStringLiteral("node_modules"), QStringLiteral("vendor"),
        QStringLiteral("build"), QStringLiteral(".idea")};
    const QFileInfoList directories =
        current.entryInfoList(QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& child : directories) {
        if (!excludedDirectories.contains(child.fileName())) {
            collectProjectFiles(child.absoluteFilePath(), files, depth + 1);
        }
        if (files.size() >= 15000) {
            return;
        }
    }
}

QIcon forgeIcon(const QString& name, const QColor& foreground, const QColor& accent) {
    QPixmap pixmap(48, 48);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(2.0, 2.0);
    painter.setPen(QPen(foreground, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    if (name == QStringLiteral("folder") || name == QStringLiteral("project")) {
        QPainterPath folder;
        folder.moveTo(3.5, 7.5);
        folder.lineTo(10, 7.5);
        folder.lineTo(12.5, 10);
        folder.lineTo(20.5, 10);
        folder.quadTo(21.5, 10, 21.5, 11.5);
        folder.lineTo(21.5, 18);
        folder.quadTo(21.5, 19.5, 20, 19.5);
        folder.lineTo(4, 19.5);
        folder.quadTo(2.5, 19.5, 2.5, 18);
        folder.lineTo(2.5, 9);
        folder.quadTo(2.5, 7.5, 3.5, 7.5);
        painter.drawPath(folder);
        painter.setPen(QPen(accent, 1.4, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(4.5, 13), QPointF(19.5, 13));
    } else if (name == QStringLiteral("file") || name == QStringLiteral("fileOpen") ||
               name.startsWith(QStringLiteral("code:"))) {
        const bool codeFile = name.startsWith(QStringLiteral("code:"));
        QPainterPath file;
        file.moveTo(6, 2.5);
        file.lineTo(14, 2.5);
        file.lineTo(19, 7.5);
        file.lineTo(19, 20.5);
        file.quadTo(19, 21.5, 18, 21.5);
        file.lineTo(6, 21.5);
        file.quadTo(5, 21.5, 5, 20.5);
        file.lineTo(5, 3.5);
        file.quadTo(5, 2.5, 6, 2.5);
        file.moveTo(14, 3);
        file.lineTo(14, 7);
        file.quadTo(14, 8, 15, 8);
        file.lineTo(18.5, 8);
        painter.drawPath(file);
        painter.setPen(QPen(accent, 1.5, Qt::SolidLine, Qt::RoundCap));
        if (codeFile) {
            painter.setFont(QFont(QStringLiteral("Segoe UI"), 5, QFont::Bold));
            painter.drawText(QRectF(5, 12, 14, 8), Qt::AlignCenter, name.mid(5));
        } else {
            painter.drawLine(QPointF(8, 12), QPointF(16, 12));
            painter.drawLine(QPointF(8, 15.5), QPointF(16, 15.5));
            painter.drawLine(QPointF(8, 19), QPointF(13, 19));
        }
    } else if (name == QStringLiteral("save")) {
        QPainterPath disk;
        disk.moveTo(4, 3);
        disk.lineTo(17.5, 3);
        disk.lineTo(21, 6.5);
        disk.lineTo(21, 20);
        disk.quadTo(21, 21, 20, 21);
        disk.lineTo(4, 21);
        disk.quadTo(3, 21, 3, 20);
        disk.lineTo(3, 4);
        disk.quadTo(3, 3, 4, 3);
        painter.drawPath(disk);
        painter.drawRect(QRectF(7, 3.5, 9, 6));
        painter.drawRect(QRectF(7, 13, 10, 7.5));
    } else if (name == QStringLiteral("settings")) {
        QPolygonF gear;
        for (int point = 0; point < 24; ++point) {
            const qreal angle = point * std::numbers::pi / 12.0;
            const qreal radius = point % 3 == 0 ? 10.5 : 8.5;
            gear << QPointF(12 + radius * std::cos(angle), 12 + radius * std::sin(angle));
        }
        painter.drawPolygon(gear);
        painter.drawEllipse(QPointF(12, 12), 3.2, 3.2);
    } else if (name == QStringLiteral("terminal")) {
        painter.drawRoundedRect(QRectF(2.5, 4, 19, 16), 2, 2);
        QPainterPath prompt;
        prompt.moveTo(6, 9);
        prompt.lineTo(9, 12);
        prompt.lineTo(6, 15);
        painter.drawPath(prompt);
        painter.drawLine(QPointF(11, 15), QPointF(16, 15));
    } else if (name == QStringLiteral("problems")) {
        QPolygonF warning;
        warning << QPointF(12, 3) << QPointF(21.5, 20) << QPointF(2.5, 20);
        painter.drawPolygon(warning);
        painter.drawLine(QPointF(12, 9), QPointF(12, 14));
        painter.setBrush(foreground);
        painter.drawEllipse(QPointF(12, 17), 0.8, 0.8);
    } else if (name == QStringLiteral("menu")) {
        painter.drawLine(QPointF(4, 6), QPointF(20, 6));
        painter.drawLine(QPointF(4, 12), QPointF(20, 12));
        painter.drawLine(QPointF(4, 18), QPointF(20, 18));
    } else if (name == QStringLiteral("debug")) {
        painter.drawRoundedRect(QRectF(7, 6, 10, 14), 4, 4);
        painter.drawEllipse(QPointF(12, 5), 3, 3);
        for (int y : {8, 13, 18}) {
            painter.drawLine(QPointF(3, y - 2), QPointF(7, y));
            painter.drawLine(QPointF(17, y), QPointF(21, y - 2));
        }
    } else if (name == QStringLiteral("plugins")) {
        for (int x : {4, 14})
            for (int y : {4, 14}) painter.drawRoundedRect(QRectF(x, y, 6, 6), 1, 1);
    } else if (name == QStringLiteral("run")) {
        QPolygonF play;
        play << QPointF(7, 4) << QPointF(20, 12) << QPointF(7, 20);
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        painter.drawPolygon(play);
    } else if (name == QStringLiteral("structure")) {
        painter.drawLine(QPointF(6, 5), QPointF(6, 18));
        painter.drawLine(QPointF(6, 9), QPointF(12, 9));
        painter.drawLine(QPointF(6, 16), QPointF(12, 16));
        painter.setBrush(accent);
        painter.drawEllipse(QPointF(6, 4), 2, 2);
        painter.drawEllipse(QPointF(14, 9), 2, 2);
        painter.drawEllipse(QPointF(14, 16), 2, 2);
    } else if (name == QStringLiteral("stop")) {
        painter.setBrush(accent);
        painter.drawRoundedRect(QRectF(5, 5, 14, 14), 2, 2);
    } else if (name == QStringLiteral("git")) {
        QPainterPath branch;
        branch.moveTo(7, 4);
        branch.lineTo(7, 18);
        branch.cubicTo(7, 12, 17, 16, 17, 9);
        painter.drawPath(branch);
        painter.setBrush(accent);
        painter.drawEllipse(QPointF(7, 4), 2.2, 2.2);
        painter.drawEllipse(QPointF(7, 19), 2.2, 2.2);
        painter.drawEllipse(QPointF(17, 8), 2.2, 2.2);
    } else if (name == QStringLiteral("close")) {
        painter.drawLine(QPointF(7, 7), QPointF(17, 17));
        painter.drawLine(QPointF(17, 7), QPointF(7, 17));
    } else if (name == QStringLiteral("search")) {
        painter.drawEllipse(QPointF(10, 10), 6, 6);
        painter.drawLine(QPointF(14.5, 14.5), QPointF(20, 20));
    } else if (name == QStringLiteral("ai")) {
        painter.drawRoundedRect(QRectF(3, 4, 18, 14), 3, 3);
        painter.drawLine(QPointF(8, 18), QPointF(6, 21));
        painter.drawLine(QPointF(9, 9), QPointF(9, 13));
        painter.drawLine(QPointF(15, 9), QPointF(15, 13));
        painter.setBrush(accent);
        painter.drawEllipse(QPointF(9, 9), 1, 1);
        painter.drawEllipse(QPointF(15, 9), 1, 1);
    } else {
        QPainterPath mark;
        mark.moveTo(5, 19);
        mark.lineTo(5, 5);
        mark.lineTo(19, 5);
        mark.moveTo(5, 12);
        mark.lineTo(15, 12);
        painter.setPen(QPen(accent, 2.5, Qt::SolidLine, Qt::SquareCap));
        painter.drawPath(mark);
    }

    return QIcon(pixmap);
}
