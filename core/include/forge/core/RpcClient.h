#pragma once
#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QJsonDocument>
#include <QHash>
#include <functional>
#include <atomic>

namespace forge::core {

/**
 * @brief JSON-RPC 2.0 client communicating over a QProcess's stdio.
 *
 * Each request gets an integer id. Responses are dispatched to the
 * registered callback. Notifications (no id) are dispatched through
 * the notificationReceived signal.
 *
 * Wire format: Content-Length header + CRLF CRLF + UTF-8 JSON body
 * (identical to LSP 3.17 framing).
 */
class RpcClient : public QObject {
    Q_OBJECT
public:
    using ResponseCallback = std::function<void(const QJsonObject& result, const QJsonObject& error)>;

    explicit RpcClient(QProcess* serviceProcess, QObject* parent = nullptr);

    /**
     * @brief Send a JSON-RPC request and register a callback for the response.
     * @param method  RPC method name (e.g. "$/ping" or "forge/index")
     * @param params  Parameters object (may be empty)
     * @param cb      Called on response — exactly once
     * @return        The request id assigned
     */
    int sendRequest(const QString& method, const QJsonObject& params, ResponseCallback cb);

    /**
     * @brief Send a JSON-RPC notification (fire-and-forget, no response expected).
     */
    void sendNotification(const QString& method, const QJsonObject& params);

signals:
    /// Emitted for every server-sent notification (requests without id from service).
    void notificationReceived(const QString& method, const QJsonObject& params);

    /// Emitted when a response arrives but no callback is registered (unexpected).
    void orphanResponse(int id, const QJsonObject& response);

private slots:
    void onReadyRead();

private:
    void processBuffer();
    void dispatchMessage(const QByteArray& json);
    QByteArray encodeMessage(const QJsonObject& obj) const;

    QProcess*                         m_process;
    QByteArray                        m_readBuffer;
    std::atomic<int>                  m_nextId{1};
    QHash<int, ResponseCallback>      m_pending;
};

} // namespace forge::core
