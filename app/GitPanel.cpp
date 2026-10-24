#include "GitPanel.h"

#include <memory>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

GitPanel::GitPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    auto* actions = new QHBoxLayout;
    auto button = [this, actions](const QString& text, const QString& name, auto callback) {
        auto* action = new QPushButton(text, this);
        action->setObjectName(name);
        actions->addWidget(action);
        connect(action, &QPushButton::clicked, this, callback);
    };
    button("Refresh", "GitRefresh", [this] { refresh(); });
    button("Stage Selected", "GitStage", [this] { stage(true); });
    button("Unstage Selected", "GitUnstage", [this] { stage(false); });
    m_staged = new QCheckBox("Staged diff", this);
    actions->addWidget(m_staged);
    connect(m_staged, &QCheckBox::toggled, this, [this] { diff(); });
    layout->addLayout(actions);
    auto* splitter = new QSplitter(this);
    m_files = new QTreeWidget(splitter);
    m_files->setObjectName("GitFiles");
    m_files->setHeaderLabels({"Index / Worktree", "File"});
    m_files->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_diff = new QPlainTextEdit(splitter);
    m_diff->setObjectName("GitDiff");
    m_diff->setReadOnly(true);
    m_diff->setLineWrapMode(QPlainTextEdit::NoWrap);
    splitter->addWidget(m_files);
    splitter->addWidget(m_diff);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter);
    m_message = new QPlainTextEdit(this);
    m_message->setObjectName("GitCommitMessage");
    m_message->setPlaceholderText("Commit message (commits the staged changes)");
    m_message->setMaximumHeight(70);
    layout->addWidget(m_message);
    auto* commitButton = new QPushButton("Commit Staged Changes", this);
    commitButton->setObjectName("GitCommit");
    connect(commitButton, &QPushButton::clicked, this, &GitPanel::commit);
    layout->addWidget(commitButton);
    m_status = new QLabel(this);
    m_status->setObjectName("GitResult");
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    connect(m_files, &QTreeWidget::itemSelectionChanged, this, &GitPanel::diff);
}
GitPanel::~GitPanel() {
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}
void GitPanel::status(const QString& text) {
    m_status->setText(text);
}
void GitPanel::run(const QStringList& arguments, Callback callback, const QByteArray& input) {
    if (m_process) {
        status("A Git operation is still running.");
        return;
    }
    const auto git = QStandardPaths::findExecutable("git");
    if (git.isEmpty()) {
        status("Git is not installed or is not on PATH.");
        return;
    }
    auto* process = new QProcess(this);
    m_process = process;
    process->setWorkingDirectory(m_root);
    auto* timer = new QTimer(process);
    timer->setSingleShot(true);
    timer->setInterval(60000);
    auto output = std::make_shared<QByteArray>();
    auto errors = std::make_shared<QByteArray>();
    auto limited = std::make_shared<bool>(false);
    auto drain = [process, output, errors, limited] {
        output->append(process->readAllStandardOutput());
        errors->append(process->readAllStandardError());
        if (output->size() + errors->size() > 4 * 1024 * 1024) {
            *limited = true;
            process->kill();
        }
    };
    connect(process, &QProcess::readyReadStandardOutput, process, drain);
    connect(process, &QProcess::readyReadStandardError, process, drain);
    connect(timer, &QTimer::timeout, process, [process, limited] {
        *limited = true;
        process->kill();
    });
    connect(process, &QProcess::started, process, [process, input, timer] {
        if (!input.isEmpty()) process->write(input);
        process->closeWriteChannel();
        timer->start();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        status(process->errorString());
        m_process = nullptr;
        process->deleteLater();
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, callback, drain, output, errors, limited](int code,
                                                                      QProcess::ExitStatus state) {
                drain();
                m_process = nullptr;
                if (*limited || state != QProcess::NormalExit) {
                    status(
                        "Git stopped (timeout, output limit or process failure). Refresh to "
                        "inspect the repository.");
                    process->deleteLater();
                    return;
                }
                callback(code, *output, *errors);
                process->deleteLater();
            });
    status("Running Git…");
    process->start(git, QStringList{"-c", "core.quotepath=false", "--no-pager"} + arguments);
}
void GitPanel::setRoot(const QString& root) {
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_files->clear();
    m_diff->clear();
    m_message->clear();
    m_root = root;
    m_resolved = false;
    if (!root.isEmpty()) refresh();
}
void GitPanel::refresh() {
    if (m_root.isEmpty()) {
        status("Open a project first.");
        return;
    }
    if (!m_resolved) {
        run({"rev-parse", "--show-toplevel"},
            [this](int code, const QByteArray& output, const QByteArray& error) {
                if (code) {
                    status(QString::fromUtf8(error));
                    return;
                }
                m_root = QString::fromUtf8(output).trimmed();
                m_resolved = true;
                refresh();
            });
        return;
    }
    run({"status", "--porcelain=v1", "-z", "--untracked-files=all"},
        [this](int code, const QByteArray& output, const QByteArray& error) {
            if (code) {
                status(QString::fromUtf8(error));
                return;
            }
            m_files->blockSignals(true);
            m_files->clear();
            const auto records = output.split('\0');
            for (int i = 0; i < records.size(); ++i) {
                const auto& record = records.at(i);
                if (record.size() < 4) continue;
                const auto path = QString::fromUtf8(record.mid(3));
                auto* row =
                    new QTreeWidgetItem(m_files, {QString::fromLatin1(record.left(2)), path});
                row->setData(0, Qt::UserRole, path);
                if (record.left(2).contains('R') || record.left(2).contains('C')) {
                    if (i + 1 < records.size())
                        row->setToolTip(1, "From " + QString::fromUtf8(records.at(++i)));
                }
            }
            m_files->blockSignals(false);
            m_files->resizeColumnToContents(0);
            status(QString::number(m_files->topLevelItemCount()) + " changed files · " + m_root);
        });
}
QStringList GitPanel::selectedPaths() const {
    QStringList paths;
    for (const auto* row : m_files->selectedItems())
        paths.append(row->data(0, Qt::UserRole).toString());
    return paths;
}
void GitPanel::diff() {
    const auto paths = selectedPaths();
    if (paths.isEmpty() || m_process) return;
    auto arguments = QStringList{"diff", "--no-ext-diff", "--no-textconv"};
    if (m_staged->isChecked()) arguments.append("--cached");
    arguments.append("--");
    arguments += paths;
    run(arguments, [this](int code, const QByteArray& output, const QByteArray& error) {
        m_diff->setPlainText(QString::fromUtf8(code ? error : output));
        status(code ? QString::fromUtf8(error)
                    : "Diff loaded. Untracked files appear after staging.");
    });
}
void GitPanel::stage(bool add) {
    const auto paths = selectedPaths();
    if (paths.isEmpty()) {
        status("Select files first.");
        return;
    }
    auto perform = [this, paths](const QStringList& prefix) {
        run(prefix + QStringList{"--"} + paths,
            [this](int code, const QByteArray&, const QByteArray& error) {
                if (code)
                    status(QString::fromUtf8(error));
                else
                    refresh();
            });
    };
    if (add)
        perform({"add"});
    else
        run({"rev-parse", "--verify", "HEAD"},
            [perform](int code, const QByteArray&, const QByteArray&) {
                perform(code ? QStringList{"rm", "--cached", "--ignore-unmatch"}
                             : QStringList{"reset", "-q", "HEAD"});
            });
}
void GitPanel::commit() {
    if (m_message->toPlainText().trimmed().isEmpty()) {
        status("Enter a commit message first.");
        return;
    }
    run(
        {"commit", "--file=-"},
        [this](int code, const QByteArray& output, const QByteArray& error) {
            if (code) {
                status(QString::fromUtf8(error));
                return;
            }
            m_diff->setPlainText(QString::fromUtf8(output + error));
            m_message->clear();
            refresh();
        },
        m_message->toPlainText().toUtf8());
}
