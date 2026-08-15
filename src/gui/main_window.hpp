#ifndef TITANAI_MAIN_WINDOW_HPP
#define TITANAI_MAIN_WINDOW_HPP

#include <QMainWindow>
#include <QSettings>

#include "agent/agent.hpp"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextBrowser;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void onSendClicked();
    void onResponseChunk(const QString &chunk);
    void onResponseReceived(const QString &response);
    void onErrorOccurred(const QString &error);
    void onModelReady(const QString &model);
    void onModelError(const QString &error);
    void onInstallRequested(const QStringList &packages);
    void onToolOutput(const QString &line);
    void onBrowseProject();
    void onBuildAndFixClicked();

private:
    void setInputEnabled(bool enabled);
    void appendMessage(const QString &sender, const QString &text, const QString &color);
    void appendPlainLine(const QString &text, const QString &color);
    void startStreamingBlock();

    Agent m_agent;
    QTextBrowser *m_chatDisplay;
    QLineEdit *m_input;
    QPushButton *m_sendButton;
    QLabel *m_statusLabel;
    QCheckBox *m_autoFixCheck{nullptr};
    QLineEdit *m_projectEdit{nullptr};
    QPushButton *m_browseButton{nullptr};
    QLineEdit *m_buildEdit{nullptr};
    QPushButton *m_buildFixButton{nullptr};
    QSettings m_settings{QStringLiteral("TitanAI"), QStringLiteral("TitanAI")};
    QString m_projectDirectory;
    QString m_buildCommand;
    bool m_modelReady{false};
    bool m_streamActive{false};
    bool m_streamBlockStarted{false};
};

#endif // TITANAI_MAIN_WINDOW_HPP
