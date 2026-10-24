#pragma once
#include <QHash>
#include <QJsonObject>
#include <QWidget>

#include "forge/core/DiagnosticsModel.h"
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
namespace forge::tools {
QHash<QString, QVector<forge::core::Diagnostic>> parseReport(const QJsonObject& report,
                                                             const QString& source,
                                                             const QString& root);
}
class InspectionPanel final : public QWidget {
    Q_OBJECT
public:
    InspectionPanel(forge::core::DiagnosticsModel* model, QWidget* parent = nullptr);
    ~InspectionPanel() override;
    void setRoot(const QString& root);

private:
    void run();
    forge::core::DiagnosticsModel* m_model;
    QComboBox* m_tool;
    QLineEdit* m_php;
    QLineEdit* m_script;
    QPlainTextEdit* m_output;
    QProcess* m_process = nullptr;
    QString m_root;
};
