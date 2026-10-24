#pragma once
#include <memory>
#include <QWidget>
class QProcess;
class QLineEdit;
class QPlainTextEdit;
class QCheckBox;
class QTableWidget;
class QLabel;
class QListWidget;
namespace forge::core {
class RpcClient;
}
class DatabasePanel final : public QWidget {
    Q_OBJECT
public:
    explicit DatabasePanel(QWidget* parent = nullptr);
    ~DatabasePanel() override;

private:
    void connectDatabase();
    void query(bool schema = false);
    void display(const QJsonObject& result, bool schema);
    void sendConnect();
    QProcess* m_process;
    std::unique_ptr<forge::core::RpcClient> m_rpc;
    QLineEdit* m_url;
    QLineEdit* m_password;
    QCheckBox* m_readOnly;
    QPlainTextEdit* m_sql;
    QTableWidget* m_results;
    QLabel* m_status;
    QListWidget* m_tables;
    QString m_backend;
    bool m_connected = false;
    bool m_busy = false;
};
