#ifndef TITANAI_AGENT_HPP
#define TITANAI_AGENT_HPP

#include <QObject>
#include <QString>
#include <QStringList>
#include <QImage>
#include "llm/ollama_client.hpp"
#include "llm/ollama_manager.hpp"
#include "tools/code_fixer.hpp"
#include "tools/file_organizer.hpp"
#include "tools/package_manager.hpp"
#include "tools/system_info.hpp"
#include "learning/task_tracker.hpp"
#include "learning/activity_analyzer.hpp"
#include "learning/suggestion_engine.hpp"
#include "calendar/calendar_manager.hpp"
#include "calendar/notification_manager.hpp"

class Agent : public QObject {
    Q_OBJECT

public:
    static constexpr const char *kDefaultModel = "gemma3:4b";

    explicit Agent(QObject *parent = nullptr);
    ~Agent() override = default;

    void sendMessage(const QString &message);
    void sendImageMessage(const QImage &image, const QString &text);
    void initializeModel(const QString &model);
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

signals:
    void responseChunkReceived(const QString &chunk);
    void responseReceived(const QString &response);
    void errorOccurred(const QString &error);
    void modelStatusChanged(OllamaManager::Status status, const QString &message);
    void modelReady(const QString &model);
    void modelError(const QString &error);
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

private slots:
    void onModelReady(const QString &model);
    void onPackageManagerFinished(bool success, const QString &summary);
    void onCodeFixBuildFinished(bool success, const QString &output);
    void onCompletionReceived(const QString &response);
    void onCompletionError(const QString &error);

private:
    bool handleSystemInfoQuery(const QString &message);
    bool handleCameraQuery(const QString &message);
    bool handleCalendarQuery(const QString &message);
    bool handleFileOrganizationQuery(const QString &message);
    bool handlePackageInstallQuery(const QString &message);
    bool handleAutoFixToggleQuery(const QString &message);
    bool handleCodeFixRequestQuery(const QString &message);
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
    bool m_autoFixEnabled{false};
    bool m_codeFixInProgress{false};
    QString m_projectDirectory;
    QString m_buildCommand;
};

#endif // TITANAI_AGENT_HPP
