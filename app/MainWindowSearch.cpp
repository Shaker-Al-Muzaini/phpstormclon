#include <QCheckBox>
#include <QDialogButtonBox>
#include <QShortcut>
#include <QTabBar>
#include <QTimer>
#include <QTreeWidget>

#include "FindBar.h"
#include "forge/core/ProjectSearch.h"
#include "MainWindow.h"
#include "WorkspaceSupport.h"

void MainWindow::findInCurrentDocument() {
    m_findBar->open(currentEditor());
}
void MainWindow::findNextInCurrentDocument() {
    m_findBar->find();
}
void MainWindow::goToLine() {
    auto* editor = currentEditor();
    if (!editor) return;
    bool ok = false;
    const int line = QInputDialog::getInt(this, "Go to Line", "Line number",
                                          editor->textCursor().blockNumber() + 1, 1,
                                          editor->blockCount(), 1, &ok);
    if (ok) {
        editor->setTextCursor(QTextCursor(editor->document()->findBlockByNumber(line - 1)));
        editor->centerCursor();
        editor->setFocus();
    }
}
void MainWindow::goToFile() {
    showQuickOpen();
}
void MainWindow::showRecentFiles() {
    if (m_recentFiles.isEmpty()) {
        statusBar()->showMessage("No recent files yet.", 3000);
        return;
    }
    bool ok = false;
    const auto path =
        QInputDialog::getItem(this, "Recent Files", "File", m_recentFiles, 0, false, &ok);
    if (ok) openFilePath(path);
}
void MainWindow::showQuickOpen(bool actionsOnly) {
    QDialog dialog(this, Qt::Popup);
    dialog.setObjectName("QuickOpenDialog");
    dialog.setWindowTitle("Search Everywhere");
    dialog.resize(660, 430);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(14, 14, 14, 12);
    auto* tabs = new QTabBar(&dialog);
    tabs->addTab("All");
    tabs->addTab("Files");
    tabs->addTab("Actions");
    tabs->setExpanding(false);
    tabs->setCurrentIndex(actionsOnly ? 2 : 0);
    layout->addWidget(tabs);
    auto* query = new QLineEdit(&dialog);
    query->setObjectName("QuickOpenQuery");
    query->setPlaceholderText("Search files and actions…");
    layout->addWidget(query);
    auto* results = new QListWidget(&dialog);
    results->setObjectName("QuickOpenResults");
    results->setUniformItemSizes(true);
    layout->addWidget(results, 1);
    auto* hint = new QLabel(
        "Enter to open  ·  Esc to close  ·  Files are indexed in the background", &dialog);
    layout->addWidget(hint);
    auto update = [this, query, results, tabs, hint]() {
        results->clear();
        const QString term = query->text().trimmed();
        if (tabs->currentIndex() != 2) {
            int count = 0;
            for (const auto& file : m_projectSearch->files()) {
                const QString relative = QDir(m_projectRoot).relativeFilePath(file);
                if (!relative.contains(term, Qt::CaseInsensitive)) continue;
                auto* row = new QListWidgetItem(m_fileIconProvider->icon(QFileInfo(file)), relative,
                                                results);
                row->setData(Qt::UserRole, file);
                row->setData(Qt::UserRole + 1, "file");
                if (++count >= 120) break;
            }
        }
        if (tabs->currentIndex() != 1) {
            auto ids = m_actions->allActionIds();
            ids.sort();
            for (const auto& id : ids) {
                auto* action = m_actions->action(id);
                if (!action->isEnabled() || !action->text().contains(term, Qt::CaseInsensitive))
                    continue;
                auto* row = new QListWidgetItem(
                    action->icon(),
                    action->text() + "    " + action->shortcut().toString(QKeySequence::NativeText),
                    results);
                row->setData(Qt::UserRole, id);
                row->setData(Qt::UserRole + 1, "action");
            }
        }
        if (results->count()) results->setCurrentRow(0);
        hint->setText(
            QStringLiteral("%1 results  ·  Enter to open  ·  Esc to close").arg(results->count()));
    };
    connect(query, &QLineEdit::textChanged, &dialog, update);
    connect(tabs, &QTabBar::currentChanged, &dialog, update);
    connect(m_projectSearch, &forge::core::ProjectSearch::filesReady, &dialog, update);
    connect(query, &QLineEdit::returnPressed, &dialog, &QDialog::accept);
    connect(results, &QListWidget::itemActivated, &dialog, &QDialog::accept);
    auto* down = new QShortcut(QKeySequence(Qt::Key_Down), query);
    connect(down, &QShortcut::activated, &dialog, [results]() { results->setFocus(); });
    update();
    dialog.move(mapToGlobal(QPoint((width() - dialog.width()) / 2, 100)));
    query->setFocus();
    if (dialog.exec() != QDialog::Accepted || !results->currentItem()) return;
    const auto id = results->currentItem()->data(Qt::UserRole).toString();
    const auto kind = results->currentItem()->data(Qt::UserRole + 1).toString();
    if (kind == "file")
        openFilePath(id);
    else if (auto* action = m_actions->action(id))
        QTimer::singleShot(0, action, &QAction::trigger);
}
void MainWindow::showFindInFiles() {
    if (m_projectRoot.isEmpty()) {
        openProject();
        if (m_projectRoot.isEmpty()) return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle("Find in Files");
    dialog.setObjectName("FindInFilesDialog");
    dialog.resize(850, 550);
    auto* layout = new QVBoxLayout(&dialog);
    auto* row = new QHBoxLayout;
    auto* query = new QLineEdit(&dialog);
    query->setPlaceholderText("Search project text…");
    query->setObjectName("ProjectSearchQuery");
    auto* matchCase = new QCheckBox("Match case", &dialog);
    auto* regex = new QCheckBox("Regex", &dialog);
    auto* search = new QPushButton("Find", &dialog);
    auto* cancel = new QPushButton("Cancel Search", &dialog);
    row->addWidget(query, 1);
    row->addWidget(matchCase);
    row->addWidget(regex);
    row->addWidget(search);
    row->addWidget(cancel);
    layout->addLayout(row);
    auto* info = new QLabel(
        "Excludes vendor, node_modules, build, .git, target and virtual environments. Searches "
        "files up to 2 MiB; results capped at 2,000.",
        &dialog);
    info->setWordWrap(true);
    layout->addWidget(info);
    auto* results = new QTreeWidget(&dialog);
    results->setHeaderLabels({"File", "Line", "Match"});
    results->setColumnWidth(0, 300);
    results->setColumnWidth(1, 55);
    layout->addWidget(results, 1);
    auto run = [this, query, matchCase, regex, info, results]() {
        results->clear();
        info->setText("Searching…");
        m_projectSearch->findText(query->text(), matchCase->isChecked(), regex->isChecked());
    };
    connect(search, &QPushButton::clicked, &dialog, run);
    connect(query, &QLineEdit::returnPressed, &dialog, run);
    connect(cancel, &QPushButton::clicked, m_projectSearch, &forge::core::ProjectSearch::cancel);
    connect(m_projectSearch, &forge::core::ProjectSearch::searchFinished, &dialog,
            [this, results, info](const QVector<forge::core::SearchHit>& hits, bool truncated) {
                for (const auto& hit : hits) {
                    auto* row = new QTreeWidgetItem(results,
                                                    {QDir(m_projectRoot).relativeFilePath(hit.path),
                                                     QString::number(hit.line), hit.preview});
                    row->setData(0, Qt::UserRole, hit.path);
                    row->setData(1, Qt::UserRole, hit.column);
                }
                info->setText(QStringLiteral("%1 matches%2")
                                  .arg(hits.size())
                                  .arg(truncated ? " (limit reached)" : ""));
            });
    connect(m_projectSearch, &forge::core::ProjectSearch::searchError, info, &QLabel::setText);
    connect(results, &QTreeWidget::itemActivated, &dialog,
            [this, &dialog](QTreeWidgetItem* item, int) {
                const auto path = item->data(0, Qt::UserRole).toString();
                const int line = item->text(1).toInt();
                const int column = item->data(1, Qt::UserRole).toInt();
                openFilePath(path);
                if (auto* editor = currentEditor()) {
                    const auto block = editor->document()->findBlockByNumber(line - 1);
                    if (block.isValid()) {
                        QTextCursor cursor(block);
                        cursor.setPosition(block.position() +
                                           qMin(column, static_cast<int>(block.length() - 1)));
                        editor->setTextCursor(cursor);
                        editor->centerCursor();
                    }
                }
                dialog.accept();
            });
    query->setFocus();
    dialog.exec();
    m_projectSearch->cancel();
}
