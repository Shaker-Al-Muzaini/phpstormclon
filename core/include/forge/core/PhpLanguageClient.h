#pragma once

#include <memory>
#include <QHash>
#include <QJsonArray>
#include <QProcess>
#include <QStringList>

#include "forge/core/RpcClient.h"

namespace forge::core {

class PhpLanguageClient final : public QObject {
    Q_OBJECT
public:
    struct Configuration {
        QString node;
        QString script;
        QString workspace;
        QString storage;
        QString phpVersion = QStringLiteral("8.3.0");
        bool clearCache = false;
    };

    explicit PhpLanguageClient(QObject* parent = nullptr);
    ~PhpLanguageClient() override;
    void start(const Configuration& configuration);
    void stop();
    bool isReady() const {
        return m_ready;
    }
    QString status() const {
        return m_status;
    }
    QJsonObject capabilities() const {
        return m_capabilities;
    }
    static QString uriForFile(const QString& file);
    void setDocument(const QString& file, const QString& text, int version);
    void closeDocument(const QString& file);
    void filesChanged(const QStringList& files, int type = 2);
    int request(const QString& method, const QJsonObject& parameters,
                RpcClient::ValueResponseCallback callback, int timeoutMs = 15000);
    void cancel(int requestId);

signals:
    void stateChanged(bool ready, const QString& message);
    void diagnosticsReceived(const QJsonObject& parameters);
    void logMessage(const QString& message);

private:
    struct Document {
        QString text;
        int version = 1;
        bool sent = false;
    };
    void beginStart();
    void initialize();
    void publishDocument(const QString& uri, Document& document);
    void setState(bool ready, const QString& message);
    QJsonObject settings() const;
    void handleServerRequest(const QJsonValue& id, const QString& method,
                             const QJsonObject& parameters);
    void handleNotification(const QString& method, const QJsonObject& parameters);

    QProcess m_process;
    std::unique_ptr<RpcClient> m_rpc;
    Configuration m_configuration;
    QHash<QString, Document> m_documents;
    QJsonObject m_capabilities;
    QString m_status;
    int m_generation = 0;
    bool m_ready = false;
    bool m_startPending = false;
    bool m_stopping = false;
};

}  // namespace forge::core
