#pragma once

#include <QColor>
#include <QIcon>
#include <QPlainTextEdit>
#include <QWidget>

#include "forge/core/DiagnosticsModel.h"

class QComboBox;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

class ProblemsPanel final : public QWidget {
    Q_OBJECT
public:
    explicit ProblemsPanel(forge::core::DiagnosticsModel* model, bool phpLintEnabled = true,
                           QWidget* parent = nullptr);
    void setCurrentDocument(const QString& file, const QString& text);
    void setSeverityColors(const QColor& error, const QColor& warning, const QColor& info);

signals:
    void locationActivated(const QString& file, int line, int column);
    void phpLintToggled(bool enabled);

private:
    void refresh();
    void showDetails(QTreeWidgetItem* item);
    QIcon severityIcon(forge::core::DiagnosticSeverity severity) const;

    forge::core::DiagnosticsModel* m_model;
    QComboBox* m_filter = nullptr;
    QTreeWidget* m_tree = nullptr;
    QLabel* m_status = nullptr;
    QPlainTextEdit* m_details = nullptr;
    QString m_currentFile;
    QString m_currentText;
    QColor m_errorColor = QColor(QStringLiteral("#E06C75"));
    QColor m_warningColor = QColor(QStringLiteral("#E5C07B"));
    QColor m_infoColor = QColor(QStringLiteral("#61AFEF"));
};
