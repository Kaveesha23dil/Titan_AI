#include "gui/main_window.hpp"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_chatDisplay(new QTextBrowser(this))
    , m_input(new QLineEdit(this))
    , m_sendButton(new QPushButton(QStringLiteral("Send"), this))
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

    m_input->setPlaceholderText(QStringLiteral("Type a message... (Enter to send)"));
    m_input->setMinimumHeight(32);

    auto *inputRow = new QHBoxLayout;
    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(m_sendButton);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->addWidget(header);
    layout->addWidget(m_chatDisplay, 1);
    layout->addLayout(inputRow);
    setCentralWidget(central);

    connect(m_sendButton, &QPushButton::clicked, this, &MainWindow::onSendClicked);
    connect(m_input, &QLineEdit::returnPressed, this, &MainWindow::onSendClicked);
    connect(&m_agent, &Agent::responseReceived, this, &MainWindow::onResponseReceived);
    connect(&m_agent, &Agent::errorOccurred, this, &MainWindow::onErrorOccurred);

    appendMessage(QStringLiteral("TitanAI"),
                  QStringLiteral("Welcome! Type a message below, or ask about your system "
                                 "(e.g. \"show system info\", \"how much RAM do I have?\")."),
                  QStringLiteral("#4a90d9"));

    m_input->setFocus();
}

void MainWindow::onSendClicked()
{
    QString text = m_input->text().trimmed();
    if (text.isEmpty()) {
        return;
    }

    m_input->clear();
    appendMessage(QStringLiteral("You"), text, QStringLiteral("#2c3e50"));
    setInputEnabled(false);
    m_agent.sendMessage(text);
}

void MainWindow::onResponseReceived(const QString &response)
{
    appendMessage(QStringLiteral("TitanAI"), response, QStringLiteral("#4a90d9"));
    setInputEnabled(true);
}

void MainWindow::onErrorOccurred(const QString &error)
{
    appendMessage(QStringLiteral("Error"), error, QStringLiteral("#c0392b"));
    setInputEnabled(true);
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
