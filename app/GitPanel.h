#pragma once
#include <functional>
#include <QWidget>
class QTreeWidget;
class QPlainTextEdit;
class QLineEdit;
class QCheckBox;
class QLabel;
class QProcess;
class GitPanel final : public QWidget {
    Q_OBJECT
public:
    explicit GitPanel(QWidget* parent = nullptr);
    ~GitPanel() override;
    void setRoot(const QString& root);
    void refresh();
    bool isBusy() const {
        return m_process != nullptr;
    }

private:
    using Callback = std::function<void(int, const QByteArray&, const QByteArray&)>;
    void run(const QStringList& arguments, Callback callback, const QByteArray& input = {});
    void diff();
    void stage(bool add);
    void commit();
    QStringList selectedPaths() const;
    void status(const QString& text);
    QTreeWidget* m_files;
    QPlainTextEdit* m_diff;
    QPlainTextEdit* m_message;
    QCheckBox* m_staged;
    QLabel* m_status;
    QProcess* m_process = nullptr;
    QString m_root;
    bool m_resolved = false;
};
