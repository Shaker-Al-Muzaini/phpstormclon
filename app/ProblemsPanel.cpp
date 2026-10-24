#include "ProblemsPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

ProblemsPanel::ProblemsPanel(forge::core::DiagnosticsModel* model, bool phpLintEnabled,
                             QWidget* parent)
    : QWidget(parent), m_model(model) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    auto* controls = new QHBoxLayout;
    controls->addWidget(new QLabel(QStringLiteral("Problems"), this));
    m_filter = new QComboBox(this);
    m_filter->addItems({QStringLiteral("All"), QStringLiteral("Errors"),
                        QStringLiteral("Warnings"), QStringLiteral("Current file")});
    controls->addWidget(m_filter);
    auto* phpLint = new QCheckBox(QStringLiteral("PHP syntax check on save"), this);
    phpLint->setObjectName(QStringLiteral("PhpLintOnSave"));
    phpLint->setChecked(phpLintEnabled);
    controls->addWidget(phpLint);
    controls->addStretch();
    layout->addLayout(controls);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("ProblemsStatus"));
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto* splitter = new QSplitter(Qt::Vertical, this);
    m_tree = new QTreeWidget(splitter);
    m_tree->setObjectName(QStringLiteral("ProblemsTree"));
    m_tree->setHeaderLabels({QStringLiteral("Problem"), QStringLiteral("Location")});
    m_tree->setColumnWidth(0, 650);
    m_details = new QPlainTextEdit(splitter);
    m_details->setObjectName(QStringLiteral("ProblemDetails"));
    m_details->setReadOnly(true);
    m_details->setMaximumBlockCount(100);
    splitter->addWidget(m_tree);
    splitter->addWidget(m_details);
    splitter->setSizes({300, 120});
    layout->addWidget(splitter, 1);

    connect(m_filter, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &ProblemsPanel::refresh);
    connect(m_model, &forge::core::DiagnosticsModel::diagnosticsChanged, this,
            &ProblemsPanel::refresh);
    connect(m_model, &forge::core::DiagnosticsModel::statusMessageChanged, this,
            &ProblemsPanel::refresh);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this]() {
        showDetails(m_tree->currentItem());
    });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                if (!item || item->data(0, Qt::UserRole + 2).toString().isEmpty()) {
                    return;
                }
                emit locationActivated(item->data(0, Qt::UserRole + 2).toString(),
                                       item->data(0, Qt::UserRole + 3).toInt(),
                                       item->data(0, Qt::UserRole + 4).toInt());
            });
    connect(phpLint, &QCheckBox::toggled, this, &ProblemsPanel::phpLintToggled);
    refresh();
}

void ProblemsPanel::setCurrentDocument(const QString& file, const QString& text) {
    m_currentFile = file;
    m_currentText = text;
    if (m_filter->currentIndex() == 3) {
        refresh();
    }
}

void ProblemsPanel::setSeverityColors(const QColor& error, const QColor& warning,
                                      const QColor& info) {
    m_errorColor = error;
    m_warningColor = warning;
    m_infoColor = info;
    refresh();
}

void ProblemsPanel::refresh() {
    const QTreeWidgetItem* previous = m_tree->currentItem();
    const QString previousFile =
        previous ? previous->data(0, Qt::UserRole + 2).toString() : QString();
    const int previousLine = previous ? previous->data(0, Qt::UserRole + 3).toInt() : 0;
    m_tree->clear();
    m_status->setText(m_model->statusMessage());

    const int filter = m_filter->currentIndex();
    const auto all = m_model->diagnostics();
    QTreeWidgetItem* runtimeRoot = nullptr;
    QHash<QString, QTreeWidgetItem*> fileRoots;
    int shown = 0;
    for (const auto& diagnostic : all) {
        const bool currentFile =
            !m_currentFile.isEmpty() &&
            QDir::cleanPath(QFileInfo(diagnostic.file).absoluteFilePath()) ==
                QDir::cleanPath(QFileInfo(m_currentFile).absoluteFilePath());
        if ((filter == 1 && diagnostic.severity != forge::core::DiagnosticSeverity::Error) ||
            (filter == 2 && diagnostic.severity != forge::core::DiagnosticSeverity::Warning) ||
            (filter == 3 && !currentFile)) {
            continue;
        }

        QTreeWidgetItem* group = nullptr;
        if (diagnostic.source == QStringLiteral("laravel.log")) {
            if (!runtimeRoot) {
                runtimeRoot =
                    new QTreeWidgetItem(m_tree, {QStringLiteral("Runtime errors"), QString()});
                runtimeRoot->setExpanded(true);
            }
            group = runtimeRoot;
        } else {
            if (!fileRoots.contains(diagnostic.file)) {
                const auto info = QFileInfo(diagnostic.file);
                auto* root = new QTreeWidgetItem(
                    m_tree,
                    {info.fileName().isEmpty() ? diagnostic.file : info.fileName(), QString()});
                root->setToolTip(0, diagnostic.file);
                root->setExpanded(true);
                fileRoots.insert(diagnostic.file, root);
            }
            group = fileRoots.value(diagnostic.file);
        }
        auto* item = new QTreeWidgetItem(
            group, {diagnostic.message,
                    QStringLiteral("%1:%2:%3").arg(QFileInfo(diagnostic.file).fileName())
                        .arg(diagnostic.line)
                        .arg(diagnostic.col)});
        item->setIcon(0, severityIcon(diagnostic.severity));
        item->setToolTip(0, diagnostic.message);
        item->setData(0, Qt::UserRole, diagnostic.message);
        item->setData(0, Qt::UserRole + 1, diagnostic.source);
        item->setData(0, Qt::UserRole + 2, diagnostic.file);
        item->setData(0, Qt::UserRole + 3, diagnostic.line);
        item->setData(0, Qt::UserRole + 4, diagnostic.col);
        item->setData(0, Qt::UserRole + 5, diagnostic.code);
        item->setData(0, Qt::UserRole + 6, static_cast<int>(diagnostic.severity));
        ++shown;

        for (const auto& frame : diagnostic.frames) {
            auto* frameItem = new QTreeWidgetItem(
                item, {QStringLiteral("↳ %1").arg(frame.label),
                       QStringLiteral("%1:%2").arg(QFileInfo(frame.file).fileName()).arg(frame.line)});
            frameItem->setData(0, Qt::UserRole + 2, frame.file);
            frameItem->setData(0, Qt::UserRole + 3, frame.line);
            frameItem->setData(0, Qt::UserRole + 4, 1);
        }
    }
    if (shown == 0 && m_model->statusMessage().isEmpty()) {
        m_status->setText(QStringLiteral("No problems found"));
    }
    for (int index = 0; index < m_tree->topLevelItemCount(); ++index) {
        auto* root = m_tree->topLevelItem(index);
        for (int child = 0; child < root->childCount(); ++child) {
            auto* item = root->child(child);
            if (item->data(0, Qt::UserRole + 2).toString() == previousFile &&
                item->data(0, Qt::UserRole + 3).toInt() == previousLine) {
                m_tree->setCurrentItem(item);
                return;
            }
        }
    }
    m_details->clear();
}

void ProblemsPanel::showDetails(QTreeWidgetItem* item) {
    if (!item || item->data(0, Qt::UserRole + 2).toString().isEmpty()) {
        m_details->clear();
        return;
    }
    const QString file = item->data(0, Qt::UserRole + 2).toString();
    const int line = item->data(0, Qt::UserRole + 3).toInt();
    const int column = item->data(0, Qt::UserRole + 4).toInt();
    const QString message = item->data(0, Qt::UserRole).toString();
    const QString source = item->data(0, Qt::UserRole + 1).toString();
    const QString code = item->data(0, Qt::UserRole + 5).toString();

    QString contents;
    if (file == m_currentFile) {
        contents = m_currentText;
    } else {
        QFile input(file);
        if (input.open(QIODevice::ReadOnly)) {
            contents = QString::fromUtf8(input.readAll());
        }
    }
    const QStringList lines = contents.split(QLatin1Char('\n'));
    QStringList details{
        QStringLiteral("%1:%2:%3").arg(file).arg(line).arg(column), message,
        QStringLiteral("Source: %1%2")
            .arg(source, code.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(code)),
        QString()};
    const int first = qMax(1, line - 3);
    const int last = qMin(lines.size(), line + 3);
    for (int index = first; index <= last; ++index) {
        details.append(QStringLiteral("%1 %2 | %3")
                           .arg(index == line ? QStringLiteral(">") : QStringLiteral(" "))
                           .arg(index, 4)
                           .arg(lines.at(index - 1)));
    }
    m_details->setPlainText(details.join(QLatin1Char('\n')));
}

QIcon ProblemsPanel::severityIcon(forge::core::DiagnosticSeverity severity) const {
    const QColor color = severity == forge::core::DiagnosticSeverity::Error
                             ? m_errorColor
                             : severity == forge::core::DiagnosticSeverity::Warning ? m_warningColor
                                                                                     : m_infoColor;
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(QRectF(2, 2, 12, 12));
    painter.setPen(QPen(Qt::white, 1.5));
    painter.drawLine(QPoint(8, 4), QPoint(8, 9));
    painter.drawPoint(QPoint(8, 11));
    return QIcon(pixmap);
}
