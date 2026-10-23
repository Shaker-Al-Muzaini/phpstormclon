#pragma once
#include <atomic>
#include <memory>
#include <QObject>
#include <QStringList>
#include <QVector>
namespace forge::core {
struct SearchHit {
    QString path;
    QString preview;
    int line = 1;
    int column = 0;
};
class ProjectSearch final : public QObject {
    Q_OBJECT
public:
    explicit ProjectSearch(QObject* parent = nullptr);
    ~ProjectSearch() override;
    void setRoot(const QString& root);
    QStringList files() const {
        return m_files;
    }
    QString root() const {
        return m_root;
    }
    void findText(const QString& query, bool caseSensitive, bool regex);
    void cancel();
signals:
    void filesReady();
    void searchFinished(const QVector<forge::core::SearchHit>& hits, bool truncated);
    void searchError(const QString& error);

private:
    QString m_root;
    QStringList m_files;
    std::shared_ptr<std::atomic_bool> m_indexCancel;
    std::shared_ptr<std::atomic_bool> m_searchCancel;
};
}  // namespace forge::core
