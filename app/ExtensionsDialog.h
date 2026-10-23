#pragma once
#include <QDialog>
#include <QJsonObject>
#include <QNetworkAccessManager>
class QListWidget;
class QLabel;
class QLineEdit;
class QComboBox;
class QPushButton;
namespace forge::core {
class ExtensionManager;
}
class ExtensionsDialog final : public QDialog {
    Q_OBJECT
public:
    explicit ExtensionsDialog(forge::core::ExtensionManager* manager, QWidget* parent = nullptr);

private:
    void refresh();
    void search();
    void downloadSelected();
    void request(const QUrl& url, bool archive);
    void report(const QString& text);
    forge::core::ExtensionManager* m_manager;
    QNetworkAccessManager m_network;
    QListWidget* m_installed;
    QListWidget* m_results;
    QLabel* m_message;
    QLineEdit* m_query;
    QComboBox* m_category;
    QPushButton* m_install;
    bool m_busy = false;
};
