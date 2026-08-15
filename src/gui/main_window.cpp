#include "gui/main_window.hpp"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
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

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->addWidget(header);
    layout->addWidget(m_statusLabel);
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
    if (enabled) {
        m_input->setFocus();
    }
}

void MainWindow::appendMessage(const QString &sender, const QString &text, const QString &color)
{
    m_chatDisplay->append(
        QStringLiteral("<b style=\"color:%1\">%2:</b> %3").arg(color, sender, text.toHtmlEscaped()));
    m_chatDisplay->verticalScrollBar()->setValue(m_chatDisplay->verticalScrollBar()->maximum());
}

void MainWindow::appendPlainLine(const QString &text, const QString &color)
{
    m_chatDisplay->append(
        QStringLiteral("<span style=\"color:%1\">%2</span>").arg(color, text.toHtmlEscaped()));
    m_chatDisplay->verticalScrollBar()->setValue(m_chatDisplay->verticalScrollBar()->maximum());
}
