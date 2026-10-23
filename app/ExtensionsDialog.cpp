#include "ExtensionsDialog.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkReply>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>

#include "forge/core/ExtensionManager.h"

ExtensionsDialog::ExtensionsDialog(forge::core::ExtensionManager* manager, QWidget* parent)
    : QDialog(parent), m_manager(manager) {
    setWindowTitle("Extensions");
    resize(780, 540);
    setMinimumSize(620, 420);
    auto* layout = new QVBoxLayout(this);
    auto* heading = new QLabel("<h2>Extensions</h2>Personalize your workspace", this);
    layout->addWidget(heading);
    auto* tabs = new QTabWidget(this);
    auto* installed = new QWidget(tabs);
    auto* installedLayout = new QVBoxLayout(installed);
    m_installed = new QListWidget(installed);
    installedLayout->addWidget(m_installed);
    auto* controls = new QHBoxLayout;
    auto* import = new QPushButton("Install from VSIX / ZIP…", installed);
    auto* folder = new QPushButton("Install from Folder…", installed);
    auto* toggle = new QPushButton("Enable / Disable", installed);
    auto* remove = new QPushButton("Uninstall", installed);
    for (auto* button : {import, folder, toggle, remove}) controls->addWidget(button);
    installedLayout->addLayout(controls);
    tabs->addTab(installed, "Installed");
    auto* marketplace = new QWidget(tabs);
    auto* marketplaceLayout = new QVBoxLayout(marketplace);
    auto* searchRow = new QHBoxLayout;
    m_query = new QLineEdit(marketplace);
    m_query->setPlaceholderText("Search Open VSX extensions…");
    m_category = new QComboBox(marketplace);
    m_category->addItems({"Themes", "Snippets", "Keymaps"});
    auto* searchButton = new QPushButton("Search", marketplace);
    searchRow->addWidget(m_query, 1);
    searchRow->addWidget(m_category);
    searchRow->addWidget(searchButton);
    marketplaceLayout->addLayout(searchRow);
    m_results = new QListWidget(marketplace);
    marketplaceLayout->addWidget(m_results);
    m_install = new QPushButton("Install Selected…", marketplace);
    marketplaceLayout->addWidget(m_install, 0, Qt::AlignRight);
    tabs->addTab(marketplace, "Marketplace");
    layout->addWidget(tabs);
    auto* compatibility = new QLabel(
        "Supports declarative themes, snippets and mapped keybindings. Extensions requiring "
        "JavaScript, native DLLs, or a language-server host cannot run here yet. VS Code theme "
        "imports support common UI colors and syntax scopes; include files and semantic-token "
        "rules are not supported.",
        this);
    compatibility->setWordWrap(true);
    layout->addWidget(compatibility);
    m_message = new QLabel("Marketplace connects only when you search or install.", this);
    m_message->setWordWrap(true);
    layout->addWidget(m_message);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(searchButton, &QPushButton::clicked, this, &ExtensionsDialog::search);
    connect(m_query, &QLineEdit::returnPressed, this, &ExtensionsDialog::search);
    connect(m_install, &QPushButton::clicked, this, &ExtensionsDialog::downloadSelected);
    connect(import, &QPushButton::clicked, this, [this]() {
        const auto path = QFileDialog::getOpenFileName(this, "Install Extension", {},
                                                       "Extension packages (*.vsix *.zip)");
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024) {
            report("Cannot read package, or package exceeds 64 MiB.");
            return;
        }
        if (QMessageBox::question(
                this, "Install Extension",
                "Install supported declarative contributions from this local package? Review the "
                "publisher and package license before installing.") != QMessageBox::Yes)
            return;
        QString error;
        report(m_manager->installArchive(file.readAll(), &error)
                   ? "Extension installed. Themes and snippets are available immediately."
                   : error);
    });
    connect(folder, &QPushButton::clicked, this, [this]() {
        const auto path = QFileDialog::getExistingDirectory(this, "Extension Folder");
        if (path.isEmpty()) return;
        QString error;
        report(m_manager->installFolder(path, &error) ? "Extension installed." : error);
    });
    connect(toggle, &QPushButton::clicked, this, [this]() {
        auto* item = m_installed->currentItem();
        if (!item) return;
        const auto manifest = item->data(Qt::UserRole).toJsonObject();
        QString error;
        report(m_manager->setEnabled(manifest.value("id").toString(),
                                     !manifest.value("enabled").toBool(), &error)
                   ? "Extension state updated."
                   : error);
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        auto* item = m_installed->currentItem();
        if (!item) return;
        const auto id = item->data(Qt::UserRole).toJsonObject().value("id").toString();
        if (QMessageBox::question(this, "Uninstall", "Remove " + id + '?') != QMessageBox::Yes)
            return;
        QString error;
        report(m_manager->uninstall(id, &error) ? "Extension removed." : error);
    });
    connect(m_manager, &forge::core::ExtensionManager::changed, this, &ExtensionsDialog::refresh);
    refresh();
}
void ExtensionsDialog::report(const QString& text) {
    m_message->setText(text);
}
void ExtensionsDialog::refresh() {
    m_installed->clear();
    for (const auto& value : m_manager->installed()) {
        const auto item = value.toObject();
        auto* row = new QListWidgetItem(
            QStringLiteral("%1  %2   ·   %3\n%4 themes · %5 snippets · %6 keybindings · %7")
                .arg(item.value("name").toString(), item.value("version").toString(),
                     item.value("enabled").toBool() ? "Enabled" : "Disabled")
                .arg(item.value("themeFiles").toArray().size())
                .arg(item.value("snippetCount").toInt())
                .arg(item.value("keybindingCount").toInt())
                .arg(item.value("license").toString()),
            m_installed);
        row->setData(Qt::UserRole, item);
    }
}
void ExtensionsDialog::search() {
    if (m_busy) return;
    QUrl url("https://open-vsx.org/api/-/search");
    QUrlQuery query;
    query.addQueryItem("query", m_query->text());
    query.addQueryItem("category", m_category->currentText());
    query.addQueryItem("size", "24");
    url.setQuery(query);
    m_results->clear();
    request(url, false);
}
void ExtensionsDialog::downloadSelected() {
    if (m_busy || !m_results->currentItem()) return;
    const auto item = m_results->currentItem()->data(Qt::UserRole).toJsonObject();
    if (QMessageBox::question(
            this, "Install from Open VSX",
            QStringLiteral("Download and install %1?\nPublisher: %2\nLicense: %3\nOnly supported "
                           "declarative contributions will be imported.")
                .arg(item.value("displayName").toString(item.value("name").toString()),
                     item.value("namespace").toString(),
                     item.value("license").toString("Read the extension license on Open VSX"))) !=
        QMessageBox::Yes)
        return;
    const QString download = item.value("files").toObject().value("download").toString();
    request(QUrl(download.isEmpty() ? item.value("url").toString() : download),
            !download.isEmpty());
}
void ExtensionsDialog::request(const QUrl& url, bool archive) {
    if (url.scheme() != "https" || url.host() != "open-vsx.org") {
        report("Invalid registry URL.");
        return;
    }
    m_busy = true;
    m_install->setEnabled(false);
    report(archive ? "Downloading package…" : "Loading registry…");
    QNetworkRequest request(url);
    request.setRawHeader("Accept-Encoding", "identity");
    request.setTransferTimeout(30000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    auto* reply = m_network.get(request);
    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64 total) {
        if (received > 64 * 1024 * 1024 || total > 64 * 1024 * 1024) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, archive]() {
        m_busy = false;
        m_install->setEnabled(true);
        const auto error = reply->error();
        const auto errorText = reply->errorString();
        const auto bytes = reply->readAll();
        reply->deleteLater();
        if (error != QNetworkReply::NoError) {
            report("Registry unavailable: " + errorText +
                   ". Local installation remains available.");
            return;
        }
        if (archive) {
            QString why;
            report(m_manager->installArchive(bytes, &why)
                       ? "Installed. Choose the new theme in Settings or insert its snippets from "
                         "Code."
                       : why);
            return;
        }
        const auto object = QJsonDocument::fromJson(bytes).object();
        if (object.contains("extensions")) {
            for (const auto& value : object.value("extensions").toArray()) {
                const auto item = value.toObject();
                auto* row = new QListWidgetItem(
                    QStringLiteral("%1  ·  %2\n%3")
                        .arg(item.value("displayName").toString(item.value("name").toString()),
                             item.value("namespace").toString(),
                             item.value("description").toString()),
                    m_results);
                row->setData(Qt::UserRole, item);
            }
            report(QStringLiteral("%1 results from Open VSX").arg(m_results->count()));
        } else {
            const QString download = object.value("files").toObject().value("download").toString();
            if (download.isEmpty())
                report("The registry did not provide a compatible download.");
            else
                this->request(QUrl(download), true);
        }
    });
}
