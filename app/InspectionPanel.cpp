#include "InspectionPanel.h"

#include <memory>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include "forge/core/Settings.h"

namespace forge::tools {
QHash<QString, QVector<forge::core::Diagnostic>> parseReport(const QJsonObject& report,
                                                             const QString& source,
                                                             const QString& root) {
    QHash<QString, QVector<forge::core::Diagnostic>> result;
    const auto files = report.value("files").toObject();
    for (auto it = files.begin(); it != files.end(); ++it) {
        const auto path = QDir::cleanPath(QDir(root).absoluteFilePath(it.key()));
        for (const auto& value : it.value().toObject().value("messages").toArray()) {
            const auto message = value.toObject();
            forge::core::Diagnostic diagnostic;
            diagnostic.file = path;
            diagnostic.source = source;
            diagnostic.line = qMax(1, message.value("line").toInt(1));
            diagnostic.col = qMax(1, message.value("column").toInt(1));
            diagnostic.endLine = diagnostic.line;
            diagnostic.endCol = diagnostic.col + 1;
            diagnostic.message = message.value("message").toString();
            diagnostic.code =
                message.value(source == "PHPStan" ? "identifier" : "source").toString();
            diagnostic.severity = message.value("type").toString() == "WARNING"
                                      ? forge::core::DiagnosticSeverity::Warning
                                      : forge::core::DiagnosticSeverity::Error;
            result[path].append(diagnostic);
        }
        if (!result.contains(path)) result.insert(path, {});
    }
    return result;
}
}  // namespace forge::tools
namespace {
QByteArray fingerprint(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return hash.result();
}
}  // namespace
InspectionPanel::InspectionPanel(forge::core::DiagnosticsModel* model, QWidget* parent)
    : QWidget(parent), m_model(model) {
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    m_tool = new QComboBox(this);
    m_tool->setObjectName("InspectionTool");
    m_tool->addItems({"PHPStan", "PHP_CodeSniffer"});
    m_php = new QLineEdit(forge::core::Settings::instance()
                              ->value("php.executable", QStandardPaths::findExecutable("php"))
                              .toString(),
                          this);
    m_php->setObjectName("InspectionPhp");
    m_script = new QLineEdit(this);
    m_script->setObjectName("InspectionScript");
    m_script->setPlaceholderText("Project vendor/bin script or a .phar file");
    form->addRow("Tool", m_tool);
    form->addRow("PHP executable", m_php);
    form->addRow("Tool script", m_script);
    layout->addLayout(form);
    auto* description = new QLabel(
        "Uses your project's installed tools and configuration. Save PHP files before checking. No "
        "Composer packages are installed automatically.",
        this);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto* buttons = new QHBoxLayout;
    auto* runButton = new QPushButton("Check Project", this);
    auto* cancel = new QPushButton("Cancel", this);
    runButton->setObjectName("InspectionRun");
    buttons->addWidget(runButton);
    buttons->addWidget(cancel);
    layout->addLayout(buttons);
    m_output = new QPlainTextEdit(this);
    m_output->setObjectName("InspectionOutput");
    m_output->setReadOnly(true);
    m_output->setMaximumBlockCount(5000);
    layout->addWidget(m_output);
    connect(runButton, &QPushButton::clicked, this, &InspectionPanel::run);
    connect(cancel, &QPushButton::clicked, this, [this] {
        if (m_process) m_process->kill();
    });
    connect(m_tool, &QComboBox::currentIndexChanged, this, [this] {
        m_script->setText(QDir(m_root).filePath(m_tool->currentIndex() == 0 ? "vendor/bin/phpstan"
                                                                            : "vendor/bin/phpcs"));
    });
}
InspectionPanel::~InspectionPanel() {
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}
void InspectionPanel::setRoot(const QString& root) {
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_root = root;
    m_script->setText(QDir(root).filePath(m_tool->currentIndex() == 0 ? "vendor/bin/phpstan"
                                                                      : "vendor/bin/phpcs"));
    m_model->clearSource("PHPStan");
    m_model->clearSource("PHP_CodeSniffer");
}
void InspectionPanel::run() {
    if (m_process) return;
    if (m_root.isEmpty() || !QFileInfo::exists(m_script->text())) {
        m_output->setPlainText(
            "Open a project and choose an installed PHPStan / PHP_CodeSniffer script. Install "
            "these using the project's normal Composer workflow if needed.");
        return;
    }
    // Disk tools must never publish findings against an unsaved editor buffer.
    for (auto* editor : window()->findChildren<QPlainTextEdit*>())
        if (editor->property("forgeSourceEditor").toBool() && editor->document()->isModified()) {
            m_output->setPlainText("Save all source files before running this disk-based tool.");
            return;
        }
    const auto source =
        m_tool->currentIndex() == 0 ? QString("PHPStan") : QString("PHP_CodeSniffer");
    QHash<QString, QByteArray> hashes;
    for (auto* editor : window()->findChildren<QPlainTextEdit*>()) {
        const auto path = editor->property("filePath").toString();
        if (editor->property("forgeSourceEditor").toBool()) hashes.insert(path, fingerprint(path));
    }
    auto* process = new QProcess(this);
    m_process = process;
    process->setWorkingDirectory(m_root);
    auto output = std::make_shared<QByteArray>();
    auto errors = std::make_shared<QByteArray>();
    auto exceeded = std::make_shared<bool>(false);
    auto drain = [process, output, errors, exceeded] {
        output->append(process->readAllStandardOutput());
        errors->append(process->readAllStandardError());
        if (output->size() + errors->size() > 16 * 1024 * 1024) {
            *exceeded = true;
            process->kill();
        }
    };
    connect(process, &QProcess::readyReadStandardOutput, process, drain);
    connect(process, &QProcess::readyReadStandardError, process, drain);
    auto* timer = new QTimer(process);
    timer->setSingleShot(true);
    timer->setInterval(120000);
    connect(timer, &QTimer::timeout, process, &QProcess::kill);
    connect(process, &QProcess::started, timer, qOverload<>(&QTimer::start));
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_output->setPlainText(process->errorString());
            m_process = nullptr;
            process->deleteLater();
        }
    });
    m_output->setPlainText("Running " + source + "…");
    connect(
        process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this, process, source, output, errors, hashes, exceeded, drain,
         started = QDateTime::currentDateTimeUtc()](int code, QProcess::ExitStatus state) {
            drain();
            m_process = nullptr;
            if (state != QProcess::NormalExit || *exceeded) {
                m_output->setPlainText(
                    "Check cancelled, timed out, or exceeded the output limit. Previous findings "
                    "were retained.");
                process->deleteLater();
                return;
            }
            QJsonParseError parseError;
            const auto document = QJsonDocument::fromJson(*output, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
                !document.object().value("files").isObject()) {
                m_output->setPlainText(
                    QString("%1 exited %2 without a valid JSON report.\n").arg(source).arg(code) +
                    QString::fromUtf8((*errors + *output).left(16000)));
                process->deleteLater();
                return;
            }
            m_model->clearSource(source);
            const auto findings = forge::tools::parseReport(document.object(), source, m_root);
            int count = 0;
            int skipped = 0;
            for (auto it = findings.cbegin(); it != findings.cend(); ++it) {
                bool stale = hashes.contains(it.key())
                                 ? fingerprint(it.key()) != hashes.value(it.key())
                                 : QFileInfo(it.key()).lastModified().toUTC() > started;
                for (auto* editor : window()->findChildren<QPlainTextEdit*>())
                    if (editor->property("forgeSourceEditor").toBool() &&
                        editor->property("filePath").toString() == it.key() &&
                        editor->document()->isModified())
                        stale = true;
                if (stale) {
                    ++skipped;
                    continue;
                }
                m_model->setFileDiagnostics(it.key(), source, it.value());
                count += static_cast<int>(it.value().size());
            }
            m_output->setPlainText(
                QString("%1 · exit %2 · %3 findings · %4 changed files skipped\n")
                    .arg(source)
                    .arg(code)
                    .arg(count)
                    .arg(skipped) +
                QString::fromUtf8(*errors) +
                QString::fromUtf8(QJsonDocument(document.object()).toJson(QJsonDocument::Indented))
                    .left(24000));
            process->deleteLater();
        });
    auto arguments = QStringList{m_script->text()};
    if (source == "PHPStan")
        arguments += {"analyse", "--no-progress", "--error-format=json"};
    else
        arguments += {"--report=json", "-q"};
    const QStringList configs =
        source == "PHPStan"
            ? QStringList{"phpstan.neon", "phpstan.neon.dist", "phpstan.dist.neon"}
            : QStringList{"phpcs.xml", "phpcs.xml.dist", ".phpcs.xml", ".phpcs.xml.dist"};
    bool configured = false;
    for (const auto& config : configs)
        configured |= QFileInfo::exists(QDir(m_root).filePath(config));
    if (!configured) {
        if (source == "PHPStan") arguments.append("--level=5");
        arguments.append(".");
    }
    forge::core::Settings::instance()->setValue("php.executable", m_php->text());
    forge::core::Settings::instance()->save();
    process->start(m_php->text(), arguments);
}
