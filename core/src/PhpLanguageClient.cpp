#include "forge/core/PhpLanguageClient.h"

#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>

namespace forge::core {

PhpLanguageClient::PhpLanguageClient(QObject* parent) : QObject(parent), m_process(this) {
    connect(&m_process, &QProcess::started, this, &PhpLanguageClient::initialize);
    connect(&m_process, &QProcess::readyReadStandardError, this, [this]() {
        const auto bytes = m_process.readAllStandardError();
        if (!bytes.isEmpty()) emit logMessage(QString::fromUtf8(bytes.left(8192)));
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            setState(false, QStringLiteral("PHP language server: %1").arg(m_process.errorString()));
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
                for (auto& document : m_documents) document.sent = false;
                if (!m_stopping)
                    setState(false, QStringLiteral("PHP language server exited (%1).").arg(code));
                else if (m_status.startsWith(QStringLiteral("Stopping")))
                    setState(false, QStringLiteral("PHP language server stopped."));
                if (m_startPending) QTimer::singleShot(0, this, &PhpLanguageClient::beginStart);
            });
    setState(false, QStringLiteral("PHP intelligence is not enabled."));
}

PhpLanguageClient::~PhpLanguageClient() {
    m_process.disconnect(this);
    if (m_rpc) m_rpc->disconnect(this);
    m_rpc.reset();
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}

QString PhpLanguageClient::uriForFile(const QString& file) {
    return QUrl::fromLocalFile(QFileInfo(file).absoluteFilePath()).toString(QUrl::FullyEncoded);
}

void PhpLanguageClient::setState(bool ready, const QString& message) {
    m_ready = ready;
    m_status = message;
    emit stateChanged(ready, message);
}

void PhpLanguageClient::start(const Configuration& configuration) {
    stop();
    m_configuration = configuration;
    m_startPending = true;
    if (m_process.state() == QProcess::NotRunning) beginStart();
}

void PhpLanguageClient::beginStart() {
    if (!m_startPending || m_process.state() != QProcess::NotRunning) return;
    ++m_generation;
    m_startPending = false;
    m_stopping = false;
    m_capabilities = {};
    if (m_configuration.node.isEmpty() || !QFileInfo::exists(m_configuration.script)) {
        setState(false,
                 QStringLiteral("Install the PHP language server and configure Node.js first."));
        return;
    }
    if (!QDir().mkpath(m_configuration.storage) ||
        (!m_configuration.globalStorage.isEmpty() &&
         !QDir().mkpath(m_configuration.globalStorage))) {
        setState(false, QStringLiteral("Cannot create the PHP index cache directory."));
        return;
    }
    m_rpc = std::make_unique<RpcClient>(&m_process, this);
    connect(m_rpc.get(), &RpcClient::notificationReceived, this,
            &PhpLanguageClient::handleNotification);
    connect(m_rpc.get(), &RpcClient::serverRequestReceived, this,
            &PhpLanguageClient::handleServerRequest);
    connect(m_rpc.get(), &RpcClient::protocolError, this, [this](const QString& error) {
        setState(false, QStringLiteral("PHP language protocol: %1").arg(error));
    });
    m_process.setWorkingDirectory(m_configuration.workspace);
    setState(false, QStringLiteral("Starting PHP intelligence…"));
    m_process.start(m_configuration.node, {m_configuration.script, QStringLiteral("--stdio")});
}

void PhpLanguageClient::stop() {
    ++m_generation;
    m_startPending = false;
    m_stopping = true;
    const bool wasReady = m_ready;
    m_ready = false;
    if (wasReady) setState(false, QStringLiteral("Stopping PHP intelligence…"));
    for (auto& document : m_documents) document.sent = false;
    if (m_process.state() == QProcess::NotRunning) return;
    const int generation = m_generation;
    if (m_rpc && m_process.state() == QProcess::Running) {
        m_rpc->sendRequestValue(
            QStringLiteral("shutdown"), {},
            [this, generation](const QJsonValue&, const QJsonObject&) {
                if (generation != m_generation || !m_rpc) return;
                m_rpc->sendNotification(QStringLiteral("exit"), {});
                m_process.closeWriteChannel();
            },
            1000);
    }
    QTimer::singleShot(1500, this, [this, generation]() {
        if (generation == m_generation && m_process.state() != QProcess::NotRunning)
            m_process.terminate();
    });
    QTimer::singleShot(2500, this, [this, generation]() {
        if (generation == m_generation && m_process.state() != QProcess::NotRunning)
            m_process.kill();
    });
}

QJsonObject PhpLanguageClient::settings() const {
    return {
        {QStringLiteral("intelephense"),
         QJsonObject{{QStringLiteral("telemetry"), QJsonObject{{QStringLiteral("enabled"), false}}},
                     {QStringLiteral("environment"),
                      QJsonObject{{QStringLiteral("phpVersion"), m_configuration.phpVersion}}},
                     {QStringLiteral("files"),
                      QJsonObject{{QStringLiteral("maxSize"), 2 * 1024 * 1024},
                                  {QStringLiteral("exclude"),
                                   QJsonArray{"**/.git/**", "**/node_modules/**", "**/build/**",
                                              "**/target/**", "**/vendor/**/{Tests,tests}/**",
                                              "**/vendor/**/vendor/**"}}}}}},
        {QStringLiteral("telemetry"), QJsonObject{{QStringLiteral("enableTelemetry"), false}}}};
}

void PhpLanguageClient::initialize() {
    const int generation = m_generation;
    const QJsonObject completionItem{
        {QStringLiteral("snippetSupport"), false},
        {QStringLiteral("insertReplaceSupport"), true},
        {QStringLiteral("documentationFormat"), QJsonArray{"plaintext"}},
        {QStringLiteral("resolveSupport"),
         QJsonObject{{QStringLiteral("properties"), QJsonArray{"additionalTextEdits", "detail"}}}}};
    const QJsonObject textDocument{
        {QStringLiteral("completion"),
         QJsonObject{{QStringLiteral("completionItem"), completionItem}}},
        {QStringLiteral("definition"), QJsonObject{{QStringLiteral("linkSupport"), true}}},
        {QStringLiteral("publishDiagnostics"),
         QJsonObject{{QStringLiteral("versionSupport"), true}}},
        {QStringLiteral("hover"),
         QJsonObject{{QStringLiteral("contentFormat"), QJsonArray{"plaintext"}}}}};
    const auto rootUri = uriForFile(m_configuration.workspace);
    const QJsonObject parameters{
        {QStringLiteral("processId"), QJsonValue(QJsonValue::Null)},
        {QStringLiteral("clientInfo"), QJsonObject{{QStringLiteral("name"), "Forge IDE"}}},
        {QStringLiteral("rootUri"), rootUri},
        {QStringLiteral("workspaceFolders"),
         QJsonArray{QJsonObject{
             {QStringLiteral("uri"), rootUri},
             {QStringLiteral("name"), QFileInfo(m_configuration.workspace).fileName()}}}},
        {QStringLiteral("capabilities"),
         QJsonObject{
             {QStringLiteral("textDocument"), textDocument},
             {QStringLiteral("workspace"), QJsonObject{{QStringLiteral("configuration"), true},
                                                       {QStringLiteral("workspaceFolders"), true}}},
             {QStringLiteral("window"), QJsonObject{{QStringLiteral("workDoneProgress"), true}}},
             {QStringLiteral("general"),
              QJsonObject{{QStringLiteral("positionEncodings"), QJsonArray{"utf-16"}}}}}},
        {QStringLiteral("initializationOptions"),
         QJsonObject{{QStringLiteral("storagePath"), m_configuration.storage},
                     {QStringLiteral("globalStoragePath"), m_configuration.globalStorage.isEmpty()
                                                               ? m_configuration.storage
                                                               : m_configuration.globalStorage},
                     {QStringLiteral("clearCache"), m_configuration.clearCache}}}};
    m_rpc->sendRequestValue(
        QStringLiteral("initialize"), parameters,
        [this, generation](const QJsonValue& result, const QJsonObject& error) {
            if (generation != m_generation || m_stopping) return;
            if (!error.isEmpty()) {
                setState(false, error.value(QStringLiteral("message")).toString());
                stop();
                return;
            }
            m_capabilities = result.toObject().value("capabilities").toObject();
            m_rpc->sendNotification(QStringLiteral("initialized"), {});
            m_rpc->sendNotification(QStringLiteral("workspace/didChangeConfiguration"),
                                    {{QStringLiteral("settings"), settings()}});
            m_ready = true;
            for (auto it = m_documents.begin(); it != m_documents.end(); ++it)
                publishDocument(it.key(), it.value());
            setState(true, QStringLiteral("PHP intelligence ready."));
        },
        30000);
}

void PhpLanguageClient::publishDocument(const QString& uri, Document& document) {
    if (!m_ready || !m_rpc) return;
    QJsonObject identity{{QStringLiteral("uri"), uri},
                         {QStringLiteral("version"), document.version}};
    if (!document.sent) {
        identity.insert(QStringLiteral("languageId"), QStringLiteral("php"));
        identity.insert(QStringLiteral("text"), document.text);
        m_rpc->sendNotification(QStringLiteral("textDocument/didOpen"),
                                {{"textDocument", identity}});
        document.sent = true;
    } else {
        m_rpc->sendNotification(
            QStringLiteral("textDocument/didChange"),
            {{"textDocument", identity},
             {"contentChanges", QJsonArray{QJsonObject{{"text", document.text}}}}});
    }
}

void PhpLanguageClient::setDocument(const QString& file, const QString& text, int version) {
    if (file.isEmpty()) return;
    if (text.size() > 2 * 1024 * 1024 || text.toUtf8().size() > 2 * 1024 * 1024) {
        closeDocument(file);
        emit logMessage(QStringLiteral("Live PHP intelligence is limited to files up to 2 MiB: %1")
                            .arg(QFileInfo(file).fileName()));
        return;
    }
    const auto uri = uriForFile(file);
    auto it = m_documents.find(uri);
    if (it == m_documents.end())
        it = m_documents.insert(uri, {text, version, false});
    else {
        if (version <= it->version) return;
        it->text = text;
        it->version = version;
    }
    publishDocument(uri, it.value());
}

void PhpLanguageClient::closeDocument(const QString& file) {
    const auto uri = uriForFile(file);
    const auto it = m_documents.find(uri);
    if (it == m_documents.end()) return;
    if (it->sent && m_ready && m_rpc)
        m_rpc->sendNotification(QStringLiteral("textDocument/didClose"),
                                {{"textDocument", QJsonObject{{"uri", uri}}}});
    m_documents.erase(it);
}

int PhpLanguageClient::request(const QString& method, const QJsonObject& parameters,
                               RpcClient::ValueResponseCallback callback, int timeoutMs) {
    if (!m_ready || !m_rpc) {
        if (callback)
            callback({}, {{"code", -32002}, {"message", "PHP intelligence is not ready."}});
        return -1;
    }
    return m_rpc->sendRequestValue(method, parameters, std::move(callback), timeoutMs);
}

void PhpLanguageClient::cancel(int requestId) {
    if (m_rpc && requestId >= 0) m_rpc->cancelRequest(requestId);
}

void PhpLanguageClient::filesChanged(const QStringList& files, int type) {
    if (!m_ready || !m_rpc) return;
    QJsonArray changes;
    for (const auto& file : files)
        changes.append(QJsonObject{{"uri", uriForFile(file)}, {"type", type}});
    m_rpc->sendNotification(QStringLiteral("workspace/didChangeWatchedFiles"),
                            {{"changes", changes}});
}

void PhpLanguageClient::handleServerRequest(const QJsonValue& id, const QString& method,
                                            const QJsonObject& parameters) {
    if (method == QStringLiteral("workspace/configuration")) {
        QJsonArray values;
        for (const auto& item : parameters.value("items").toArray()) {
            QJsonValue value = settings();
            for (const auto& part : item.toObject().value("section").toString().split('.'))
                if (!part.isEmpty()) value = value.toObject().value(part);
            values.append(value.isUndefined() ? QJsonValue(QJsonValue::Null) : value);
        }
        m_rpc->sendResponse(id, values);
    } else if (method == QStringLiteral("workspace/workspaceFolders")) {
        m_rpc->sendResponse(
            id, QJsonArray{QJsonObject{{"uri", uriForFile(m_configuration.workspace)},
                                       {"name", QFileInfo(m_configuration.workspace).fileName()}}});
    } else if (method == QStringLiteral("window/workDoneProgress/create")) {
        m_rpc->sendResponse(id, QJsonValue(QJsonValue::Null));
    } else if (method == QStringLiteral("window/showMessageRequest")) {
        emit logMessage(parameters.value("message").toString());
        m_rpc->sendResponse(id, QJsonValue(QJsonValue::Null));
    } else {
        m_rpc->sendResponse(id, {}, {{"code", -32601}, {"message", "Unsupported client request"}});
    }
}

void PhpLanguageClient::handleNotification(const QString& method, const QJsonObject& parameters) {
    if (method == QStringLiteral("textDocument/publishDiagnostics"))
        emit diagnosticsReceived(parameters);
    else if (method == QStringLiteral("$/progress")) {
        const auto value = parameters.value("value").toObject();
        if (value.value("kind").toString() == QStringLiteral("end"))
            setState(m_ready, QStringLiteral("PHP intelligence ready."));
        else
            setState(m_ready,
                     QStringLiteral("PHP: %1 %2 %3")
                         .arg(value.value("title").toString(), value.value("message").toString(),
                              value.contains("percentage")
                                  ? QString::number(value.value("percentage").toInt()) + '%'
                                  : QString()));
    } else if (method == QStringLiteral("window/logMessage") ||
               method == QStringLiteral("window/showMessage"))
        emit logMessage(parameters.value("message").toString());
}

}  // namespace forge::core
