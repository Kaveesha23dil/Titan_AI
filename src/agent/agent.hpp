#ifndef TITANAI_AGENT_HPP
#define TITANAI_AGENT_HPP

#include <QObject>
#include <QString>
#include <QStringList>
#include <QImage>
#include "llm/ollama_client.hpp"
#include "llm/ollama_manager.hpp"
#include "tools/code_fixer.hpp"
#include "tools/disk_cleanup.hpp"
#include "tools/file_organizer.hpp"
#include "tools/package_manager.hpp"
#include "tools/system_info.hpp"
#include "tools/ui_developer.hpp"
#include "tools/update_checker.hpp"
#include "tools/translation_assistant.hpp"
#include "tools/web_search.hpp"
#include "learning/task_tracker.hpp"
#include "learning/activity_analyzer.hpp"
#include "learning/suggestion_engine.hpp"
#include "calendar/calendar_manager.hpp"
#include "calendar/notification_manager.hpp"
#include "power/power_manager.hpp"
#include "agent/chat_history_manager.hpp"

class Agent : public QObject {
    Q_OBJECT

public:
    static constexpr const char *kDefaultModel = "qwen2.5-coder:3b";

    explicit Agent(QObject *parent = nullptr);
    ~Agent() override = default;

    void sendMessage(const QString &message);
    void sendImageMessage(const QImage &image, const QString &text);
    void initializeModel(const QString &model);
    void setModel(const QString &model);
    void switchModel(const QString &model);
    void refreshModels();
    [[nodiscard]] QString currentModel() const;
    [[nodiscard]] QStringList installedModels() const;
    void unloadModel();
    void performInstall(const QStringList &packages);
    void setAutoFixEnabled(bool enabled);
    [[nodiscard]] bool autoFixEnabled() const;
    void setProjectDirectory(const QString &directory);
    void setBuildCommand(const QString &command);
    void runBuildAndFix();
    [[nodiscard]] bool isCodeFixBusy() const;

    void startLearning();
    void stopLearning();
    [[nodiscard]] bool isLearning() const;
    [[nodiscard]] QString getStartupSuggestions() const;
    void refreshSuggestions();

    void startCalendar();
    [[nodiscard]] CalendarManager &calendarManager();
    [[nodiscard]] NotificationManager &notificationManager();
    [[nodiscard]] FileOrganizer &fileOrganizer();
    [[nodiscard]] DiskCleanup &diskCleanup();
    [[nodiscard]] UpdateChecker &updateChecker();

    // Translation Assistant
    [[nodiscard]] TranslationAssistant &translationAssistant();

    // Web Search
    [[nodiscard]] WebSearch &webSearch();

    // Chat History
    [[nodiscard]] ChatHistoryManager &chatHistoryManager();
    void startNewConversation();
    [[nodiscard]] QList<SearchResult> searchChatHistory(const QString &query,
                                                        const SearchFilter &filter = {}) const;

    // Power Management
    [[nodiscard]] PowerManager &powerManager();
    void applyPowerProfile(PowerProfile profile);

    // UI Design-to-Code & Development controls
    void setCodeDevelopmentEnabled(bool enabled);
    [[nodiscard]] bool isCodeDevelopmentEnabled() const;
    void developUi(const QImage &designImage,
                   const QString &requirements,
                   const QString &branchName,
                   UiDeveloper::Framework framework = UiDeveloper::Framework::AutoDetect);

signals:
    void responseChunkReceived(const QString &chunk);
    void responseReceived(const QString &response);
    void errorOccurred(const QString &error);
    void modelStatusChanged(OllamaManager::Status status, const QString &message);
    void modelReady(const QString &model);
    void modelError(const QString &error);
    void modelsChanged(const QStringList &models);
    void installRequested(const QStringList &packages);
    void cameraRequested();
    void toolOutputReceived(const QString &line);
    void autoFixEnabledChanged(bool enabled);
    void codeFixStatus(const QString &message);
    void codeFixFinished(const QString &summary, bool success);
    void learningStarted();
    void learningStopped();
    void startupSuggestionsReady(const QString &suggestions);
    void calendarEventsReady(const QString &eventsSummary);
    void calendarNotificationAlert(const QString &title, const QString &message);
    void powerReportReady(const QString &report);
    void powerProfileApplied(const QString &profileName);

    // UI Developer signals
    void uiDevelopmentProgress(const QString &message);
    void uiDevelopmentFinished(bool success, const QString &summary, const QString &branchName);

    // Chat History signals
    void chatHistorySearchResult(const QString &formattedResult);
    void newConversationStarted(const QString &sessionId);

    // Translation signals
    void translationResultReady(const QString &markdownResult);

    // Web Search signals
    void webSearchStarted();
    void webSearchFinished(const QString &groundedAnswer);
    void webSearchError(const QString &error);

private slots:
    void onModelReady(const QString &model);
    void onPackageManagerFinished(bool success, const QString &summary);
    void onCodeFixBuildFinished(bool success, const QString &output);
    void onCompletionReceived(const QString &response);
    void onCompletionError(const QString &error);
    void onWebSearchFinished(const QList<WebSearchResult> &results);
    void onWebSearchError(const QString &error);

private:
    bool handleWebSearchQuery(const QString &message);
    bool handleSystemInfoQuery(const QString &message);
    bool handleCameraQuery(const QString &message);
    bool handleCalendarQuery(const QString &message);
    bool handleFileOrganizationQuery(const QString &message);
    bool handleDiskCleanupQuery(const QString &message);
    bool handleUpdateCheckerQuery(const QString &message);
    bool handlePackageInstallQuery(const QString &message);
    bool handleAutoFixToggleQuery(const QString &message);
    bool handleCodeFixRequestQuery(const QString &message);
    bool handleUiDevelopmentQuery(const QString &message, const QImage &image = QImage());
    bool handlePowerQuery(const QString &message);
    bool handleChatHistoryQuery(const QString &message);
    bool handleTranslationQuery(const QString &message);
    void startPasteFix(const QString &message);
    void startBuildFix();
    void requestFix(const QList<CodeFixer::BuildError> &errors);
    void applyFixes(const QString &llmOutput);
    QString buildFixPrompt(const QList<CodeFixer::BuildError> &errors) const;
    QString resolveFilePath(const QString &path) const;
    QStringList extractPackageNames(const QString &message) const;
    static bool isInformationalQuery(const QString &lowerMessage);
    static bool isInformationalFixQuery(const QString &lowerMessage);
    static bool looksLikeErrorOutput(const QString &message);

    OllamaManager m_ollamaManager;
    OllamaClient m_ollamaClient;
    SystemInfoTool m_systemInfoTool;
    PackageManager m_packageManager;
    CodeFixer m_codeFixer;
    TaskTracker m_taskTracker;
    ActivityAnalyzer m_activityAnalyzer;
    SuggestionEngine m_suggestionEngine;
    CalendarManager m_calendarManager;
    NotificationManager m_notificationManager;
    FileOrganizer m_fileOrganizer;
    DiskCleanup m_diskCleanup;
    UpdateChecker m_updateChecker;
    UiDeveloper m_uiDeveloper;
    PowerManager m_powerManager;
    ChatHistoryManager m_chatHistoryManager;
    TranslationAssistant m_translationAssistant;
    WebSearch m_webSearch;
    bool m_codeDevelopmentEnabled{false};
    bool m_autoFixEnabled{false};
    bool m_codeFixInProgress{false};
    QString m_webSearchQuery;
    QString m_projectDirectory;
    QString m_buildCommand;
};

#endif // TITANAI_AGENT_HPP
