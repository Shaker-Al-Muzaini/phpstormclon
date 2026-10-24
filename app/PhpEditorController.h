#pragma once

#include <functional>
#include <QHash>
#include <QJsonArray>
#include <QPointer>
#include <QTextCursor>

#include "forge/core/PhpLanguageClient.h"

class QCompleter;
class QDialog;
class QLineEdit;
class QPlainTextEdit;
class QStandardItemModel;
class QTextDocument;
namespace forge::core {
class DiagnosticsModel;
}

// All LSP positions are UTF-16, as are QTextCursor positions.
namespace forge::editor {
bool applyEdits(QTextDocument* document, const QJsonArray& edits);
int position(QTextDocument* document, const QJsonObject& point);
}  // namespace forge::editor

class PhpEditorController final : public QObject {
    Q_OBJECT
public:
    PhpEditorController(QWidget* window, forge::core::DiagnosticsModel* diagnostics);
    ~PhpEditorController() override;
    void setWorkspace(const QString& workspace, bool clearCache = false);
    void attach(QPlainTextEdit* editor);
    void closeDocument(QPlainTextEdit* editor);
    void configure();
    void complete(QPlainTextEdit* editor);
    void definition(QPlainTextEdit* editor);
    void symbols();
    void format(QPlainTextEdit* editor);
    bool isReady() const {
        return m_client.isReady();
    }
    std::function<void(const QString&, int, int)> openLocation;
    std::function<void(const QString&)> message;

protected:
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    struct Document {
        QPointer<QTextDocument> document;
        QString path;
        int revision = -1;
        int version = 0;
    };
    void sync(QPlainTextEdit* editor);
    QJsonObject parameters(QPlainTextEdit* editor);
    void diagnostics(const QJsonObject& parameters);
    void navigate(const QJsonValue& value);
    void acceptCompletion(const QModelIndex& index);
    void scanWorkspace();
    void installServer(QDialog* dialog, const QString& node, QLineEdit* script);
    QPlainTextEdit* editorFor(QTextDocument* document) const;
    QWidget* m_window;
    forge::core::DiagnosticsModel* m_diagnostics;
    forge::core::PhpLanguageClient m_client;
    QHash<QTextDocument*, Document> m_documents;
    QCompleter* m_completer;
    QStandardItemModel* m_model;
    QPointer<QPlainTextEdit> m_completionEditor;
    QString m_workspace;
    int m_completionRevision = -1;
    int m_completionPosition = -1;
    int m_completionStart = -1;
    int m_completionRequest = 0;
    bool m_scanning = false;
    QHash<QString, qint64> m_diskFiles;
};
