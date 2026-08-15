#include "gui/main_window.hpp"

#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTextBrowser>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_chatDisplay(new QTextBrowser(this))
    , m_input(new QLineEdit(this))
    , m_sendButton(new QPushButton(QStringLiteral("Send"), this))
    , m_statusLabel(new QLabel(QStringLiteral("Initializing local AI model..."), this))
{
    setWindowTitle(QStringLiteral("TitanAI"));
    resize(680, 520);

    m_chatDisplay->setReadOnly(true);
    m_chatDisplay->setOpenExternalLinks(false);

    auto *header = new QLabel(QStringLiteral("TitanAI - Local AI Assistant"), this);
    header->setAlignment(Qt::AlignCenter);
    QFont headerFont = header->font();
    headerFont.setBold(true);
    headerFont.setPointSize(headerFont.pointSize() + 4);
    header->setFont(headerFont);

    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #666; font-size: 11px;"));

    m_input->setPlaceholderText(QStringLiteral("Type a message... (Enter to send)"));
    m_input->setMinimumHeight(32);

    auto *inputRow = new QHBoxLayout;
    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(m_sendButton);

    auto *fixerBox = new QGroupBox(QStringLiteral("Developer Auto-Fix"), this);
    auto *fixerLayout = new QVBoxLayout(fixerBox);

    m_autoFixCheck = new QCheckBox(QStringLiteral("Auto-fix code errors"), fixerBox);
    auto *checkRow = new QHBoxLayout;
    checkRow->addWidget(m_autoFixCheck);
    checkRow->addStretch(1);

    m_projectEdit = new QLineEdit(fixerBox);
    m_projectEdit->setPlaceholderText(QStringLiteral("e.g. /home/you/my-project"));
    m_browseButton = new QPushButton(QStringLiteral("Browse"), fixerBox);
    auto *projectRow = new QHBoxLayout;
    projectRow->addWidget(new QLabel(QStringLiteral("Project:"), fixerBox));
    projectRow->addWidget(m_projectEdit, 1);
    projectRow->addWidget(m_browseButton);

    m_buildEdit = new QLineEdit(fixerBox);
    m_buildEdit->setPlaceholderText(QStringLiteral("e.g. cmake --build build   or   npm run build"));
    m_buildFixButton = new QPushButton(QStringLiteral("Build && Fix"), fixerBox);
    auto *buildRow = new QHBoxLayout;
    buildRow->addWidget(new QLabel(QStringLiteral("Build:"), fixerBox));
    buildRow->addWidget(m_buildEdit, 1);
    buildRow->addWidget(m_buildFixButton);

    fixerLayout->addLayout(checkRow);
    fixerLayout->addLayout(projectRow);
    fixerLayout->addLayout(buildRow);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->addWidget(header);
    layout->addWidget(m_statusLabel);
    layout->addWidget(fixerBox);
    layout->addWidget(m_chatDisplay, 1);
    layout->addLayout(inputRow);
    setCentralWidget(central);

    connect(m_sendButton, &QPushButton::clicked, this, &MainWindow::onSendClicked);
    connect(m_input, &QLineEdit::returnPressed, this, &MainWindow::onSendClicked);
    connect(&m_agent, &Agent::responseChunkReceived, this, &MainWindow::onResponseChunk);
    connect(&m_agent, &Agent::responseReceived, this, &MainWindow::onResponseReceived);
    connect(&m_agent, &Agent::errorOccurred, this, &MainWindow::onErrorOccurred);
    connect(&m_agent, &Agent::installRequested, this, &MainWindow::onInstallRequested);
    connect(&m_agent, &Agent::toolOutputReceived, this, &MainWindow::onToolOutput);
    connect(&m_agent, &Agent::modelStatusChanged, this,
            [this](OllamaManager::Status, const QString &message) {
                m_statusLabel->setText(message);
            });
    connect(&m_agent, &Agent::modelReady, this, &MainWindow::onModelReady);
    connect(&m_agent, &Agent::modelError, this, &MainWindow::onModelError);

    m_projectDirectory = m_settings.value(QStringLiteral("projectDir")).toString();
    m_buildCommand = m_settings.value(QStringLiteral("buildCommand")).toString();
    const bool autoFixEnabled = m_settings.value(QStringLiteral("autoFixEnabled"), false).toBool();

    m_autoFixCheck->setChecked(autoFixEnabled);
    m_projectEdit->setText(m_projectDirectory);
    m_buildEdit->setText(m_buildCommand);
    m_agent.setAutoFixEnabled(autoFixEnabled);
    m_agent.setProjectDirectory(m_projectDirectory);
    m_agent.setBuildCommand(m_buildCommand);

    connect(m_autoFixCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_settings.setValue(QStringLiteral("autoFixEnabled"), checked);
        m_agent.setAutoFixEnabled(checked);
    });
    connect(m_projectEdit, &QLineEdit::editingFinished, this, [this]() {
        m_projectDirectory = m_projectEdit->text().trimmed();
        m_settings.setValue(QStringLiteral("projectDir"), m_projectDirectory);
        m_agent.setProjectDirectory(m_projectDirectory);
    });
    connect(m_buildEdit, &QLineEdit::editingFinished, this, [this]() {
        m_buildCommand = m_buildEdit->text().trimmed();
        m_settings.setValue(QStringLiteral("buildCommand"), m_buildCommand);
        m_agent.setBuildCommand(m_buildCommand);
    });
    connect(m_browseButton, &QPushButton::clicked, this, &MainWindow::onBrowseProject);
    connect(m_buildFixButton, &QPushButton::clicked, this, &MainWindow::onBuildAndFixClicked);
    connect(&m_agent, &Agent::autoFixEnabledChanged, this, [this](bool enabled) {
        m_settings.setValue(QStringLiteral("autoFixEnabled"), enabled);
        const QSignalBlocker blocker(m_autoFixCheck);
        m_autoFixCheck->setChecked(enabled);
    });
    connect(&m_agent, &Agent::codeFixStatus, this, [this](const QString &message) {
        appendMessage(QStringLiteral("TitanAI"), message, QStringLiteral("#4a90d9"));
    });
    connect(&m_agent, &Agent::codeFixFinished, this,
            [this](const QString &summary, bool success) {
                appendMessage(success ? QStringLiteral("TitanAI") : QStringLiteral("Error"),
                              summary,
                              success ? QStringLiteral("#4a90d9") : QStringLiteral("#c0392b"));
                setInputEnabled(true);
            });

    appendMessage(QStringLiteral("TitanAI"),
                  QStringLiteral("Welcome! Loading the local AI model. You can start chatting once "
                                 "it is ready."),
                  QStringLiteral("#4a90d9"));

    setInputEnabled(false);
    m_agent.initializeModel(Agent::kDefaultModel);
}

void MainWindow::onSendClicked()
{
    if (!m_modelReady) {
        return;
    }

    QString text = m_input->text().trimmed();
    if (text.isEmpty()) {
        return;
    }

    m_input->clear();
    appendMessage(QStringLiteral("You"), text, QStringLiteral("#2c3e50"));
    setInputEnabled(false);
    m_agent.sendMessage(text);
}

void MainWindow::onModelReady(const QString &model)
{
    m_modelReady = true;
    m_statusLabel->setText(QStringLiteral("Model ready: %1").arg(model));
    appendMessage(QStringLiteral("TitanAI"),
                  QStringLiteral("Local AI model '%1' is ready. Ask me anything about your "
                                 "system, or just chat!")
                      .arg(model),
                  QStringLiteral("#4a90d9"));
    setInputEnabled(true);
}

void MainWindow::onModelError(const QString &error)
{
    m_modelReady = false;
    m_statusLabel->setText(QStringLiteral("Model unavailable"));
    appendMessage(QStringLiteral("Error"), error, QStringLiteral("#c0392b"));
    setInputEnabled(false);
}

void MainWindow::onResponseChunk(const QString &chunk)
{
    m_streamActive = true;

    if (!m_streamBlockStarted) {
        startStreamingBlock();
    }

    QTextCursor cursor = m_chatDisplay->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(chunk);
    m_chatDisplay->setTextCursor(cursor);
    m_chatDisplay->verticalScrollBar()->setValue(m_chatDisplay->verticalScrollBar()->maximum());
}

void MainWindow::onResponseReceived(const QString &response)
{
    if (!m_streamActive) {
        appendMessage(QStringLiteral("TitanAI"), response, QStringLiteral("#4a90d9"));
    }

    m_streamActive = false;
    m_streamBlockStarted = false;
    setInputEnabled(true);
}

void MainWindow::onErrorOccurred(const QString &error)
{
    m_streamActive = false;
    m_streamBlockStarted = false;
    appendMessage(QStringLiteral("Error"), error, QStringLiteral("#c0392b"));
    setInputEnabled(true);
}

void MainWindow::onInstallRequested(const QStringList &packages)
{
    QMessageBox::StandardButton answer =
        QMessageBox::question(this,
                              QStringLiteral("Confirm Installation"),
                              QStringLiteral("Install the following package(s) on your system?\n\n"
                                             "  %1\n\n"
                                             "This runs pacman with administrator privileges.")
                                  .arg(packages.join(QStringLiteral(", "))),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No);

    if (answer != QMessageBox::Yes) {
        setInputEnabled(true);
        return;
    }

    appendMessage(QStringLiteral("TitanAI"),
                  QStringLiteral("Installing %1...").arg(packages.join(QStringLiteral(", "))),
                  QStringLiteral("#4a90d9"));
    m_agent.performInstall(packages);
}

void MainWindow::onToolOutput(const QString &line)
{
    appendPlainLine(line, QStringLiteral("#6b7280"));
}

void MainWindow::onBrowseProject()
{
    const QString startDir =
        m_projectDirectory.isEmpty() ? QDir::homePath() : m_projectDirectory;
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select Project Directory"), startDir);
    if (directory.isEmpty()) {
        return;
    }
    m_projectDirectory = directory;
    m_projectEdit->setText(directory);
    m_settings.setValue(QStringLiteral("projectDir"), directory);
    m_agent.setProjectDirectory(directory);
}

void MainWindow::onBuildAndFixClicked()
{
    if (m_agent.isCodeFixBusy()) {
        return;
    }
    if (!m_modelReady) {
        appendMessage(QStringLiteral("Error"),
                      QStringLiteral("The model is not ready yet. Please wait."),
                      QStringLiteral("#c0392b"));
        return;
    }
    setInputEnabled(false);
    m_agent.runBuildAndFix();
}

void MainWindow::startStreamingBlock()
{
    m_chatDisplay->append(QStringLiteral("<b style=\"color:#4a90d9\">TitanAI:</b> "));
    m_streamBlockStarted = true;

    QTextCursor cursor = m_chatDisplay->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_chatDisplay->setTextCursor(cursor);
}

void MainWindow::setInputEnabled(bool enabled)
{
    m_input->setEnabled(enabled);
    m_sendButton->setEnabled(enabled);
    m_buildFixButton->setEnabled(enabled);
    if (enabled) {
        m_input->setFocus();
    }
}

void MainWindow::appendMessage(const QString &sender, const QString &text, const QString &color)
{
    QString escaped = text.toHtmlEscaped();
    escaped.replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
    m_chatDisplay->append(
        QStringLiteral("<b style=\"color:%1\">%2:</b> %3").arg(color, sender, escaped));
    m_chatDisplay->verticalScrollBar()->setValue(m_chatDisplay->verticalScrollBar()->maximum());
}

void MainWindow::appendPlainLine(const QString &text, const QString &color)
{
    m_chatDisplay->append(
        QStringLiteral("<span style=\"color:%1\">%2</span>").arg(color, text.toHtmlEscaped()));
    m_chatDisplay->verticalScrollBar()->setValue(m_chatDisplay->verticalScrollBar()->maximum());
}
