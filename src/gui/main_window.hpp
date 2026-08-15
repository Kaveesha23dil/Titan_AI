#ifndef TITANAI_MAIN_WINDOW_HPP
#define TITANAI_MAIN_WINDOW_HPP

#include <QMainWindow>

#include "agent/agent.hpp"

class QTextBrowser;
class QLineEdit;
class QPushButton;
class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void onSendClicked();
    void onResponseReceived(const QString &response);
    void onErrorOccurred(const QString &error);
    void onModelReady(const QString &model);
    void onModelError(const QString &error);

private:
    void setInputEnabled(bool enabled);
    void appendMessage(const QString &sender, const QString &text, const QString &color);

    Agent m_agent;
    QTextBrowser *m_chatDisplay;
    QLineEdit *m_input;
    QPushButton *m_sendButton;
    QLabel *m_statusLabel;
    bool m_modelReady{false};
};

#endif // TITANAI_MAIN_WINDOW_HPP
