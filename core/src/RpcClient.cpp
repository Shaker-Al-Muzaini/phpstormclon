#include "forge/core/RpcClient.h"
#include <QDebug>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonParseError>

namespace forge::core {

RpcClient::RpcClient(QProcess* serviceProcess, QObject* parent)
    : QObject(parent)
    , m_process(serviceProcess)
{
    Q_ASSERT(m_process);
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &RpcClient::onReadyRead);
}

int RpcClient::sendRequest(const QString& method, const QJsonObject& params, ResponseCallback cb)
{
    const int id = m_nextId.fetch_add(1);

    QJsonObject req;
    req[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    req[QStringLiteral("id")]      = id;
    req[QStringLiteral("method")]  = method;
    if (!params.isEmpty()) {
        req[QStringLiteral("params")] = params;
    }

    m_pending.insert(id, std::move(cb));
    m_process->write(encodeMessage(req));
    return id;
}

void RpcClient::sendNotification(const QString& method, const QJsonObject& params)
{
    QJsonObject notif;
    notif[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    notif[QStringLiteral("method")]  = method;
    if (!params.isEmpty()) {
        notif[QStringLiteral("params")] = params;
    }
    m_process->write(encodeMessage(notif));
}

void RpcClient::onReadyRead()
{
    m_readBuffer.append(m_process->readAllStandardOutput());
    processBuffer();
}

void RpcClient::processBuffer()
{
    while (true) {
        // Look for the Content-Length header
        const int headerEnd = m_readBuffer.indexOf("\r\n\r\n");
        if (headerEnd == -1) break;

        const QByteArray header = m_readBuffer.left(headerEnd);
        int contentLength = -1;
        for (const auto& line : header.split('\n')) {
            const auto trimmed = line.trimmed();
            if (trimmed.startsWith("Content-Length:")) {
                bool ok = false;
                contentLength = trimmed.mid(15).trimmed().toInt(&ok);
                if (!ok) contentLength = -1;
            }
        }

        if (contentLength <= 0) {
            qWarning() << "[RpcClient] Invalid or missing Content-Length, discarding buffer";
            m_readBuffer.clear();
            break;
        }

        const int bodyStart = headerEnd + 4; // skip \r\n\r\n
        if (m_readBuffer.size() < bodyStart + contentLength) {
            break; // Wait for more data
        }

        const QByteArray body = m_readBuffer.mid(bodyStart, contentLength);
        m_readBuffer.remove(0, bodyStart + contentLength);
        dispatchMessage(body);
    }
}

void RpcClient::dispatchMessage(const QByteArray& json)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "[RpcClient] JSON parse error:" << err.errorString();
        return;
    }

    const QJsonObject obj = doc.object();

    if (obj.contains(QStringLiteral("id")) && !obj[QStringLiteral("id")].isNull()) {
        // Response
        const int id = obj[QStringLiteral("id")].toInt(-1);
        auto it = m_pending.find(id);
        if (it != m_pending.end()) {
            const QJsonObject result = obj[QStringLiteral("result")].toObject();
            const QJsonObject error  = obj[QStringLiteral("error")].toObject();
            it.value()(result, error);
            m_pending.erase(it);
        } else {
            emit orphanResponse(id, obj);
        }
    } else {
        // Notification
        const QString method = obj[QStringLiteral("method")].toString();
        const QJsonObject params = obj[QStringLiteral("params")].toObject();
        emit notificationReceived(method, params);
    }
}

QByteArray RpcClient::encodeMessage(const QJsonObject& obj) const
{
    const QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    QByteArray msg;
    msg.append(QStringLiteral("Content-Length: %1\r\n\r\n").arg(body.size()).toUtf8());
    msg.append(body);
    return msg;
}

} // namespace forge::core
