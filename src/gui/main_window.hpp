#ifndef TITANAI_MAIN_WINDOW_HPP
#define TITANAI_MAIN_WINDOW_HPP

#include <QMainWindow>
#include <QSettings>
#include <QImage>

#include "agent/agent.hpp"
#include "voice/voice_engine.hpp"

class QCheckBox;
class QLabel;
class QLineEdit;
class QProgressBar;
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

    void onVoicePartial(const QString &text);
    void onVoiceFinal(const QString &text);
    void onVoiceError(const QString &error);
    void onVoiceSettings();
    void onVoiceButtonToggled(bool enabled);
    void onCaptureFromCamera();
    void onSelectImage();
    void onClearPendingImage();
    void onStartupSuggestions(const QString &suggestions);
    void onCalendarEventsReady(const QString &eventsSummary);
    void onCalendarNotificationAlert(const QString &title, const QString &message);
    void onOpenCalendarSettings();

private:
    void setInputEnabled(bool enabled);
    void appendMessage(const QString &sender, const QString &text, const QString &color);
    void appendImage(const QImage &image);
    void appendPlainLine(const QString &text, const QString &color);
    void startStreamingBlock();
    void updateVoiceUi();
    void updatePendingImageUi();
    void saveVoiceSettings(const VoiceEngine::Config &config);
    VoiceEngine::Config loadVoiceSettings();

    Agent m_agent;
    VoiceEngine m_voiceEngine;
    QTextBrowser *m_chatDisplay;
    QLineEdit *m_input;
    QPushButton *m_sendButton;
    QLabel *m_statusLabel;
    QCheckBox *m_autoFixCheck{nullptr};
    QLineEdit *m_projectEdit{nullptr};
    QPushButton *m_browseButton{nullptr};
    QLineEdit *m_buildEdit{nullptr};
    QPushButton *m_buildFixButton{nullptr};
    QPushButton *m_voiceButton{nullptr};
    QPushButton *m_voiceSettingsButton{nullptr};
    QLabel *m_voiceStatusLabel{nullptr};
    QProgressBar *m_micLevelBar{nullptr};
    QPushButton *m_cameraButton{nullptr};
    QPushButton *m_imageButton{nullptr};
    QLabel *m_pendingImageLabel{nullptr};
    QPushButton *m_clearImageButton{nullptr};
    QPushButton *m_calendarButton{nullptr};
    QLabel *m_notificationBanner{nullptr};
    QImage m_pendingImage;
    QSettings m_settings{QStringLiteral("TitanAI"), QStringLiteral("TitanAI")};
    QString m_projectDirectory;
    QString m_buildCommand;
    bool m_modelReady{false};
    bool m_streamActive{false};
    bool m_streamBlockStarted{false};
};

#endif // TITANAI_MAIN_WINDOW_HPP
