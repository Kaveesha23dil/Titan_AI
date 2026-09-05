#ifndef TITANAI_MAIN_WINDOW_HPP
#define TITANAI_MAIN_WINDOW_HPP

#include <QMainWindow>
#include <QSettings>
#include <QImage>
#include <QSystemTrayIcon>

#include "agent/agent.hpp"
#include "voice/voice_engine.hpp"
#include "gui/chat_history_dialog.hpp"
#include "gui/translation_dialog.hpp"
#include "gui/meeting_notes_dialog.hpp"

class QCheckBox;
 class QComboBox;
 class QLabel;
 class QLineEdit;
 class QPlainTextEdit;
 class QProgressBar;
 class QPushButton;
 class QScrollArea;
 class QSlider;
 class QSpinBox;
 class QStackedWidget;
 class QTextBrowser;
 class QVBoxLayout;
 class QWidget;

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

    // Chat History slots
    void onOpenChatHistory();
    void onLoadHistorySession(const QString &sessionId);
    void onOrganizeClicked();
    void onDiskCleanupClicked();

    void onUiDevelopmentProgress(const QString &message);
    void onUiDevelopmentFinished(bool success, const QString &summary, const QString &branchName);
    void onGenerateUiClicked();
    void onUiDesignImageClicked();
    void onClearUiDesignImage();

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
    void onManageModels();
    void onModelsChanged(const QStringList &models);
    void onSaveSettings();
    void onSettingsBrowseProject();
    void onOpenTranslationAssistant();
    void onTranslateSelectedText();
    void onTranslationSendToChat(const QString &text);
    void onOpenMeetingNotes();
    void onMeetingSendToChat(const QString &text);

    // Power Management slots
    void onPowerProfileChanged(int index);
    void onBrightnessSliderChanged(int value);
    void onFreeAiRamClicked();
    void onBatteryInfoUpdated(const BatteryInfo &info);
    void onLowBatteryWarning(int percent);
    void onCriticalBatteryWarning(int percent);

    // Enhanced Update Checker slots
    void onCheckUpdatesClicked();
    void onApplyRepoUpdatesClicked();
    void onApplyAllUpdatesClicked();
    void onUpdateCheckProgress(const QString &stage);
    void onUpdateCheckFinished(int count);
    void onUpdateCheckError(const QString &error);
    void onUpdatesApplyStarted(const QString &command);
    void onUpdatesApplyOutput(const QString &line);
    void onUpdatesApplyFinished(bool success);
    void onPeriodicUpdateCheckDone(int count);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    // --- UI setup helpers ---
    void setupGlobalStylesheet();
    QWidget *createSidebar();
    QWidget *createWelcomePage();
    QWidget *createChatPage();
    QWidget *createDevHubPage();
    QWidget *createSettingsPage();
    QWidget *createInputCard();
    QIcon createVectorIcon(const QString &name, int size = 24);
    void reparentInputCard(QVBoxLayout *targetLayout);

    // --- Navigation ---
    void navigateTo(int pageIndex);

    // --- Existing helpers ---
    void setInputEnabled(bool enabled);
    void appendMessage(const QString &sender, const QString &text, const QString &color);
    void appendImage(const QImage &image);
    void appendPlainLine(const QString &text, const QString &color);
    void startStreamingBlock();
    void updateVoiceUi();
    void updatePendingImageUi();
    void saveVoiceSettings(const VoiceEngine::Config &config);
    VoiceEngine::Config loadVoiceSettings();
    void applyProjectDirectory(const QString &directory);
    void updateModelLabels(const QString &model);

    // --- Conversation export helpers ---
    [[nodiscard]] QString exportFileName(ChatHistoryManager &history,
                                         const QString &extension) const;
    void exportCurrentConversationAsMarkdown();
    void exportCurrentConversationAsPdf();

    // --- Core state ---
    Agent m_agent;
    VoiceEngine m_voiceEngine;
    QSettings m_settings{QStringLiteral("TitanAI"), QStringLiteral("TitanAI")};
    QString m_projectDirectory;
    QString m_buildCommand;
    QImage m_pendingImage;
    bool m_modelReady{false};
    bool m_streamActive{false};
    bool m_streamBlockStarted{false};
    QSystemTrayIcon *m_trayIcon{nullptr};

    // --- Navigation & Pages ---
    QStackedWidget *m_pageStack{nullptr};
    QPushButton *m_navHome{nullptr};
    QPushButton *m_navChat{nullptr};
    QPushButton *m_navDev{nullptr};
    QPushButton *m_navCalendar{nullptr};
    QPushButton *m_navVoiceSettings{nullptr};
    QPushButton *m_navSettings{nullptr};
    int m_currentPage{0};

    // --- Welcome Page widgets ---
    QLabel *m_welcomeGreeting{nullptr};
    QLabel *m_welcomeSubtitle{nullptr};
    QVBoxLayout *m_welcomeInputSlot{nullptr};

    // --- Chat Page widgets ---
    QTextBrowser *m_chatDisplay{nullptr};
    QLabel *m_statusLabel{nullptr};
    QVBoxLayout *m_chatInputSlot{nullptr};


    // --- Chat History ---
    QPushButton   *m_navHistory{nullptr};  ///< Sidebar history button (🕐)
    // --- Translation ---
    QPushButton   *m_navTranslate{nullptr}; ///< Sidebar translation button (🌐)
    // --- Meeting Notes ---
    QPushButton   *m_navMeeting{nullptr};  ///< Sidebar meeting notes button
    MeetingNotesDialog *m_meetingDialog{nullptr}; ///< Single non-modal instance

    // --- Shared Input Card ---
    QWidget *m_inputCard{nullptr};
    QLineEdit *m_input{nullptr};
    QPushButton *m_sendButton{nullptr};
    QPushButton *m_voiceButton{nullptr};
    QPushButton *m_cameraButton{nullptr};
    QPushButton *m_imageButton{nullptr};
    QLabel *m_pendingImageLabel{nullptr};
    QPushButton *m_clearImageButton{nullptr};
    QLabel *m_voiceStatusLabel{nullptr};
    QProgressBar *m_micLevelBar{nullptr};

    // --- Dev Hub widgets ---
    QLabel *m_aiModelLabel{nullptr};
    QPushButton *m_changeModelButton{nullptr};
    QCheckBox *m_autoFixCheck{nullptr};
    QLineEdit *m_projectEdit{nullptr};
    QPushButton *m_browseButton{nullptr};
    QLineEdit *m_buildEdit{nullptr};
    QPushButton *m_buildFixButton{nullptr};
    QPushButton *m_organizeButton{nullptr};
    QPushButton *m_diskCleanupButton{nullptr};
    QPushButton *m_checkUpdatesButton{nullptr};

    // --- UI Design-to-Code widgets ---
    QLabel *m_uiDesignPreview{nullptr};
    QPushButton *m_uiDesignPickBtn{nullptr};
    QPushButton *m_uiDesignClearBtn{nullptr};
    QComboBox *m_uiFrameworkCombo{nullptr};
    QLineEdit *m_uiBranchEdit{nullptr};
    QPlainTextEdit *m_uiRequirementsEdit{nullptr};
    QPushButton *m_uiGenerateButton{nullptr};
    QProgressBar *m_uiProgressBar{nullptr};
    QLabel *m_uiStatusLabel{nullptr};
    QImage m_uiDesignImage;

    // --- Notification banner ---
    QLabel *m_notificationBanner{nullptr};

    // --- Voice Settings button (kept for dialog access) ---
    QPushButton *m_voiceSettingsButton{nullptr};
    QPushButton *m_calendarButton{nullptr};

    // --- Power Management widgets ---
    QProgressBar *m_batteryBar{nullptr};
    QLabel       *m_batteryLabel{nullptr};
    QLabel       *m_powerStatusLabel{nullptr};
    QComboBox    *m_powerProfileCombo{nullptr};
    QSlider      *m_brightnessSlider{nullptr};
    QLabel       *m_brightnessValueLabel{nullptr};

    // --- Enhanced Update Checker widgets ---
    QProgressBar  *m_updateProgressBar{nullptr};
    QLabel        *m_updateStatusLabel{nullptr};
    QLabel        *m_updateCountBadge{nullptr};
    QPushButton   *m_applyRepoButton{nullptr};
    QPushButton   *m_applyAllButton{nullptr};
    QPlainTextEdit*m_updateOutputLog{nullptr};
    QSpinBox      *m_updateIntervalSpin{nullptr};

    // --- Settings Page widgets ---
    QLabel         *m_settingsModelLabel{nullptr};
    QLineEdit      *m_settingsProjectEdit{nullptr};
    QLineEdit      *m_settingsBuildEdit{nullptr};
    QCheckBox      *m_settingsAutoFixCheck{nullptr};
    QSpinBox       *m_settingsIntervalSpin{nullptr};
    QLabel         *m_settingsStatusLabel{nullptr};
};

#endif // TITANAI_MAIN_WINDOW_HPP
