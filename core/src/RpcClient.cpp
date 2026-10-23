#include "forge/core/RpcClient.h"

#include <QDebug>
#include <QJsonParseError>
#include <QJsonValue>
#include <cmath>
#include <limits>

namespace forge::core {

RpcClient::RpcClient(QProcess* serviceProcess, QObject* parent)
    : QObject(parent), m_process(serviceProcess) {
    Q_ASSERT(m_process);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &RpcClient::onReadyRead);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) { onProcessEnded(); });
    connect(m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart || error == QProcess::Crashed) {
                    onProcessEnded();
                }
            });
}

int RpcClient::sendRequest(const QString& method, const QJsonObject& params,
                           ResponseCallback callback) {
    const int id = m_nextId.fetch_add(1);
    QJsonObject request;
    request[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    request[QStringLiteral("id")] = id;
    request[QStringLiteral("method")] = method;
    if (!params.isEmpty()) {
        request[QStringLiteral("params")] = params;
    }

    const QByteArray message = encodeMessage(request);
    if (message.isEmpty()) {
        QJsonObject error{{QStringLiteral("code"), -32000},
                          {QStringLiteral("message"),
                           QStringLiteral("JSON-RPC request exceeds the 64 MiB frame limit.")}};
        if (callback) {
            callback({}, error);
        }
        return id;
    }
    m_pending.insert(id, std::move(callback));
    if (m_process->write(message) != message.size()) {
        auto pending = m_pending.find(id);
        if (pending != m_pending.end()) {
            ResponseCallback failedCallback = std::move(pending.value());
            m_pending.erase(pending);
            if (failedCallback) {
                failedCallback({}, {{QStringLiteral("code"), -32000},
                                    {QStringLiteral("message"), m_process->errorString()}});
            }
        }
    }
    return id;
}

void RpcClient::sendNotification(const QString& method, const QJsonObject& params) {
    QJsonObject notification;
    notification[QStringLiteral("jsonrpc")] = QStringLiteral("2.0");
    notification[QStringLiteral("method")] = method;
    if (!params.isEmpty()) {
        notification[QStringLiteral("params")] = params;
    }
    const QByteArray message = encodeMessage(notification);
    if (message.isEmpty()) {
        emit protocolError(QStringLiteral("JSON-RPC notification exceeds the 64 MiB frame limit."));
        return;
    }
    if (m_process->write(message) != message.size()) {
        emit protocolError(QStringLiteral("Could not write JSON-RPC notification: %1")
                               .arg(m_process->errorString()));
    }
}

void RpcClient::onReadyRead() {
    const QByteArray incoming = m_process->readAllStandardOutput();
    m_readBuffer.append(incoming);
    processBuffer();
}

void RpcClient::onProcessEnded() {
    m_readBuffer.clear();
    failPending(QStringLiteral("The JSON-RPC service process exited before replying."));
}

void RpcClient::processBuffer() {
    while (!m_readBuffer.isEmpty()) {
        const qsizetype headerEnd = m_readBuffer.indexOf(QByteArrayLiteral("\r\n\r\n"));
        if (headerEnd < 0) {
            if (m_readBuffer.size() > 8192) {
                const QString error = QStringLiteral("JSON-RPC frame header exceeds 8 KiB.");
                m_readBuffer.clear();
                emit protocolError(error);
                failPending(error);
            }
            return;
        }
        if (headerEnd > 8192) {
            const QString error = QStringLiteral("JSON-RPC frame header exceeds 8 KiB.");
            m_readBuffer.clear();
            emit protocolError(error);
            failPending(error);
            return;
        }

        const QByteArray header = m_readBuffer.first(headerEnd);
        qlonglong contentLength = -1;
        bool foundContentLength = false;
        bool invalidContentLength = false;
        for (const QByteArray& line : header.split('\n')) {
            const QByteArray trimmed = line.trimmed();
            const qsizetype colon = trimmed.indexOf(':');
            if (colon >= 0 &&
                trimmed.first(colon).trimmed().compare(QByteArrayLiteral("Content-Length"),
                                                       Qt::CaseInsensitive) == 0) {
                if (foundContentLength) {
                    invalidContentLength = true;
                    break;
                }
                foundContentLength = true;
                bool ok = false;
                contentLength = trimmed.sliced(colon + 1).trimmed().toLongLong(&ok);
                invalidContentLength = !ok;
            }
        }

        if (!foundContentLength || invalidContentLength || contentLength <= 0 ||
            contentLength > kMaxFrameBytes) {
            const QString error =
                QStringLiteral("Invalid or oversized JSON-RPC Content-Length: %1")
                    .arg(contentLength);
            qWarning() << "[RpcClient]" << error;
            m_readBuffer.clear();
            emit protocolError(error);
            failPending(error);
            return;
        }

        const qsizetype bodyStart = headerEnd + qsizetype(4);
        if (contentLength > std::numeric_limits<qsizetype>::max() - bodyStart) {
            const QString error = QStringLiteral("JSON-RPC frame length overflow.");
            m_readBuffer.clear();
            emit protocolError(error);
            failPending(error);
            return;
        }
        const qsizetype frameEnd = bodyStart + static_cast<qsizetype>(contentLength);
        if (m_readBuffer.size() < frameEnd) {
            return;
        }

        const QByteArray body =
            m_readBuffer.sliced(bodyStart, static_cast<qsizetype>(contentLength));
        m_readBuffer.remove(0, frameEnd);
        dispatchMessage(body);
    }
}

void RpcClient::dispatchMessage(const QByteArray& json) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        const QString error = QStringLiteral("Invalid JSON-RPC JSON body: %1")
                                  .arg(parseError.errorString());
        qWarning() << "[RpcClient]" << error;
        emit protocolError(error);
        failPending(error);
        return;
    }

    const QJsonObject object = document.object();
    const bool hasId = object.contains(QStringLiteral("id"));
    const bool hasMethod = object.contains(QStringLiteral("method"));
    auto rejectMessage = [this](const QString& reason) {
        qWarning() << "[RpcClient]" << reason;
        emit protocolError(reason);
        failPending(reason);
    };
    if (object.value(QStringLiteral("jsonrpc")).toString() != QStringLiteral("2.0") ||
        hasId == hasMethod) {
        rejectMessage(QStringLiteral("Malformed JSON-RPC message."));
        return;
    }

    if (hasId) {
        const QJsonValue idValue = object.value(QStringLiteral("id"));
        bool idIsValid = false;
        int id = -1;
        if (idValue.isDouble()) {
            const double numericId = idValue.toDouble();
            if (std::isfinite(numericId) && std::floor(numericId) == numericId &&
                numericId >= std::numeric_limits<int>::min() &&
                numericId <= std::numeric_limits<int>::max()) {
                id = static_cast<int>(numericId);
                idIsValid = true;
            }
        }
        const bool hasResult = object.contains(QStringLiteral("result"));
        const bool hasError = object.contains(QStringLiteral("error"));
        if (!idIsValid || hasResult == hasError ||
            (hasResult && !object.value(QStringLiteral("result")).isObject()) ||
            (hasError && !object.value(QStringLiteral("error")).isObject())) {
            rejectMessage(QStringLiteral("Malformed JSON-RPC response."));
            return;
        }

        auto pending = m_pending.find(id);
        if (pending != m_pending.end()) {
            ResponseCallback callback = std::move(pending.value());
            m_pending.erase(pending);
            const QJsonObject result = object.value(QStringLiteral("result")).toObject();
            const QJsonObject error = object.value(QStringLiteral("error")).toObject();
            if (callback) {
                callback(result, error);
            }
            return;
        }
        emit orphanResponse(id, object);
        return;
    }

    const QString method = object.value(QStringLiteral("method")).toString();
    if (method.isEmpty() ||
        (object.contains(QStringLiteral("params")) &&
         !object.value(QStringLiteral("params")).isObject())) {
        rejectMessage(QStringLiteral("Malformed JSON-RPC notification."));
        return;
    }
    emit notificationReceived(method,
                              object.value(QStringLiteral("params")).toObject());
}

QByteArray RpcClient::encodeMessage(const QJsonObject& object) const {
    const QByteArray body = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (body.size() <= 0 || body.size() > kMaxFrameBytes) {
        return {};
    }
    QByteArray message;
    message.reserve(body.size() + 32);
    message.append(QByteArrayLiteral("Content-Length: "));
    message.append(QByteArray::number(body.size()));
    message.append(QByteArrayLiteral("\r\n\r\n"));
    message.append(body);
    return message;
}

void RpcClient::failPending(const QString& reason) {
    if (m_pending.isEmpty()) {
        return;
    }
    const auto pending = std::move(m_pending);
    m_pending.clear();
    for (const ResponseCallback& callback : pending) {
        if (callback) {
            callback({}, {{QStringLiteral("code"), -32000},
                          {QStringLiteral("message"), reason}});
        }
    }
}

}  // namespace forge::core
