#include "DatabasePanel.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "forge/core/RpcClient.h"

DatabasePanel::DatabasePanel(QWidget* parent) : QWidget(parent), m_process(new QProcess(this)) {
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    m_url = new QLineEdit(this);
    m_url->setObjectName("DatabaseUrl");
    m_url->setPlaceholderText(
        "sqlite://C:/path/app.sqlite · mysql://user@localhost/db?ssl-mode=disabled");
    m_url->setEchoMode(QLineEdit::PasswordEchoOnEdit);  // URLs may include credentials.
    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText("Kept in memory only");
    m_readOnly = new QCheckBox("Read only (reconnect to change)", this);
    m_readOnly->setChecked(true);
    auto* connectButton = new QPushButton("Connect", this);
    connectButton->setObjectName("DatabaseConnect");
    form->addRow("Connection URL", m_url);
    form->addRow("Password", m_password);
    form->addRow(m_readOnly, connectButton);
    layout->addLayout(form);
    auto* splitter = new QSplitter(this);
    m_tables = new QListWidget(splitter);
    auto* queryArea = new QWidget(splitter);
    auto* queryLayout = new QVBoxLayout(queryArea);
    m_sql = new QPlainTextEdit(queryArea);
    m_sql->setObjectName("DatabaseSql");
    m_sql->setPlainText("SELECT 1 AS connected;");
    m_sql->setMaximumHeight(110);
    queryLayout->addWidget(m_sql);
    auto* buttons = new QHBoxLayout;
    auto* run = new QPushButton("Execute SQL", this);
    run->setObjectName("DatabaseExecute");
    buttons->addWidget(run);
    auto* tables = new QPushButton("Refresh Tables", this);
    buttons->addWidget(tables);
    auto* cancel = new QPushButton("Disconnect / Cancel", this);
    buttons->addWidget(cancel);
    queryLayout->addLayout(buttons);
    m_results = new QTableWidget(queryArea);
    m_results->setObjectName("DatabaseResults");
    m_results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_results->setAlternatingRowColors(true);
    queryLayout->addWidget(m_results);
    splitter->setStretchFactor(1, 3);
    layout->addWidget(splitter);
    m_status = new QLabel(
        "SQLite, MySQL and PostgreSQL · one statement · 500 rows / 4 MiB / 15 s limit", this);
    m_status->setObjectName("DatabaseStatus");
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    connect(connectButton, &QPushButton::clicked, this, &DatabasePanel::connectDatabase);
    connect(run, &QPushButton::clicked, this, [this] { query(); });
    connect(tables, &QPushButton::clicked, this, [this] { query(true); });
    connect(cancel, &QPushButton::clicked, this, [this] {
        m_process->kill();
        m_connected = false;
        m_status->setText(
            "Disconnected. If a write was running, inspect its outcome after reconnecting.");
    });
    connect(m_process, &QProcess::started, this, &DatabasePanel::sendConnect);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_busy = false;
            m_status->setText(m_process->errorString());
        }
    });
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
                m_connected = false;
                m_busy = false;
                m_status->setText("Database service disconnected.");
            });
    m_rpc = std::make_unique<forge::core::RpcClient>(m_process, this);
    connect(m_tables, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        const auto parts = item->data(Qt::UserRole).toStringList();
        QStringList quoted;
        for (auto part : parts) {
            if (m_backend == "mysql")
                quoted.append('`' + part.replace("`", "``") + '`');
            else
                quoted.append('"' + part.replace("\"", "\"\"") + '"');
        }
        m_sql->setPlainText("SELECT * FROM " + quoted.join('.') + " LIMIT 500;");
        query();
    });
}
DatabasePanel::~DatabasePanel() {
    m_process->disconnect(this);
    m_rpc.reset();
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}
void DatabasePanel::connectDatabase() {
    if (m_busy) return;
    if (m_url->text().trimmed().isEmpty()) {
        m_status->setText("Enter a connection URL first.");
        return;
    }
    m_connected = false;
    m_tables->clear();
    m_results->clear();
    if (m_process->state() == QProcess::Running) {
        sendConnect();
        return;
    }
    auto binary = QCoreApplication::applicationDirPath() + "/dbtools";
#ifdef Q_OS_WIN
    binary += ".exe";
#endif
    if (!QFileInfo::exists(binary)) {
        m_status->setText("dbtools is not built. Rebuild with FORGE_BUILD_RUST=ON.");
        return;
    }
    m_busy = true;
    m_status->setText("Starting database service…");
    m_process->start(binary, {"--stdio"});
}
void DatabasePanel::sendConnect() {
    QString url = m_url->text().trimmed();
    if (!url.startsWith("sqlite:") && !m_password->text().isEmpty()) {
        QUrl parsed(url);
        parsed.setPassword(m_password->text());
        url = parsed.toString(QUrl::FullyEncoded);
    }
    m_busy = true;
    m_status->setText("Connecting…");
    m_rpc->sendRequest(
        "database/connect", {{"url", url}, {"readOnly", m_readOnly->isChecked()}},
        [this](const QJsonObject& result, const QJsonObject& error) {
            m_busy = false;
            if (!error.isEmpty()) {
                m_status->setText(error.value("message").toString());
                return;
            }
            m_connected = result.value("connected").toBool();
            m_backend = result.value("backend").toString();
            m_status->setText(
                "Connected · " + m_backend +
                (result.value("readOnly").toBool() ? " · read only" : " · writes enabled"));
            query(true);
        });
}
void DatabasePanel::query(bool schema) {
    if (m_busy) return;
    if (!m_connected) {
        m_status->setText("Connect first.");
        return;
    }
    m_busy = true;
    m_status->setText("Executing…");
    m_rpc->sendRequestValue(
        schema ? "database/tables" : "database/query",
        schema ? QJsonObject{} : QJsonObject{{"sql", m_sql->toPlainText()}},
        [this, schema](const QJsonValue& result, const QJsonObject& error) {
            m_busy = false;
            if (!error.isEmpty()) {
                m_status->setText(error.value("message").toString());
                return;
            }
            display(result.toObject(), schema);
        },
        20000);
}
void DatabasePanel::display(const QJsonObject& result, bool schema) {
    const auto rows = result.value("rows").toArray();
    if (schema) {
        m_tables->clear();
        for (const auto& row : rows) {
            const auto values = row.toArray();
            if (values.isEmpty()) continue;
            QStringList parts;
            if (m_backend == "sqlite")
                parts.append(values.at(0).toString());
            else if (values.size() >= 2)
                parts = {values.at(0).toString(), values.at(1).toString()};
            auto* item = new QListWidgetItem(parts.join('.'), m_tables);
            item->setData(Qt::UserRole, parts);
        }
        m_status->setText(QString::number(rows.size()) + " tables / views");
        return;
    }
    QStringList columns;
    for (const auto& value : result.value("columns").toArray()) columns.append(value.toString());
    m_results->clear();
    m_results->setColumnCount(static_cast<int>(columns.size()));
    m_results->setHorizontalHeaderLabels(columns);
    m_results->setRowCount(static_cast<int>(rows.size()));
    for (int row = 0; row < rows.size(); ++row) {
        const auto cells = rows.at(row).toArray();
        for (int column = 0; column < cells.size(); ++column) {
            const auto value = cells.at(column);
            const auto text =
                value.isNull() ? "NULL"
                : value.isString()
                    ? value.toString()
                    : QString::fromUtf8(
                          QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact))
                          .mid(1)
                          .chopped(1);
            m_results->setItem(row, column, new QTableWidgetItem(text));
        }
    }
    m_results->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_status->setText(QString("%1 rows · %2 affected%3")
                          .arg(rows.size())
                          .arg(result.value("rowsAffected").toDouble())
                          .arg(result.value("truncated").toBool() ? " · result truncated" : ""));
}
