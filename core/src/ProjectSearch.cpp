#include "forge/core/ProjectSearch.h"

#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QRegularExpression>
#include <QSet>
#include <QtConcurrent/QtConcurrentRun>

#include "forge/core/TextFileCodec.h"
namespace forge::core {
ProjectSearch::ProjectSearch(QObject* parent) : QObject(parent) {}
ProjectSearch::~ProjectSearch() {
    if (m_indexCancel) *m_indexCancel = true;
    cancel();
}
void ProjectSearch::cancel() {
    if (m_searchCancel) *m_searchCancel = true;
}
void ProjectSearch::setRoot(const QString& root) {
    if (m_indexCancel) *m_indexCancel = true;
    cancel();
    m_root = root;
    m_files.clear();
    auto token = std::make_shared<std::atomic_bool>(false);
    m_indexCancel = token;
    auto* watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcher<QStringList>::finished, this, [this, watcher, token]() {
        if (!*token) {
            m_files = watcher->result();
            emit filesReady();
        }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([root, token]() {
        const QSet<QString> excluded{".git",   "vendor", "node_modules", "build",
                                     "target", ".idea",  ".forge",       ".venv"};
        QStringList files;
        QList<QPair<QString, int>> pending{{root, 0}};
        while (!pending.isEmpty() && !*token && files.size() < 30000) {
            const auto current = pending.takeLast();
            if (current.second > 40) continue;
            for (const auto& item : QDir(current.first)
                                        .entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden |
                                                           QDir::NoDotAndDotDot | QDir::NoSymLinks,
                                                       QDir::Name)) {
                if (*token || files.size() >= 30000) break;
                if (item.isDir()) {
                    if (!excluded.contains(item.fileName()))
                        pending.append({item.absoluteFilePath(), current.second + 1});
                } else
                    files.append(item.absoluteFilePath());
            }
        }
        files.sort(Qt::CaseInsensitive);
        return files;
    }));
}
void ProjectSearch::findText(const QString& query, bool caseSensitive, bool regex) {
    cancel();
    if (query.isEmpty()) {
        emit searchFinished({}, false);
        return;
    }
    QRegularExpression expression(regex ? query : QRegularExpression::escape(query));
    if (!caseSensitive) expression.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    if (!expression.isValid()) {
        emit searchError(expression.errorString());
        return;
    }
    auto token = std::make_shared<std::atomic_bool>(false);
    m_searchCancel = token;
    auto* watcher = new QFutureWatcher<QVector<SearchHit>>(this);
    connect(watcher, &QFutureWatcher<QVector<SearchHit>>::finished, this, [this, watcher, token]() {
        if (!*token) {
            const auto hits = watcher->result();
            emit searchFinished(hits, hits.size() >= 2000);
        }
        watcher->deleteLater();
    });
    const auto files = m_files;
    watcher->setFuture(QtConcurrent::run([files, expression, token]() {
        QVector<SearchHit> hits;
        for (const auto& path : files) {
            if (*token || hits.size() >= 2000) break;
            QFile file(path);
            if (file.size() > 2 * 1024 * 1024 || !file.open(QIODevice::ReadOnly)) continue;
            DecodedText decoded;
            QString error;
            if (!TextFileCodec::decode(file.readAll(), &decoded, &error) ||
                decoded.text.contains(QChar::Null))
                continue;
            const auto lines = decoded.text.split('\n');
            for (int i = 0; i < lines.size() && hits.size() < 2000 && !*token; ++i) {
                auto matches = expression.globalMatch(lines.at(i));
                while (matches.hasNext() && hits.size() < 2000) {
                    const auto match = matches.next();
                    hits.append({path, lines.at(i).left(300), i + 1,
                                 static_cast<int>(match.capturedStart())});
                }
            }
        }
        return hits;
    }));
}
}  // namespace forge::core
