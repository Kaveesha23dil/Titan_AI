#ifndef TITANAI_AUTOMATION_PLANNER_HPP
#define TITANAI_AUTOMATION_PLANNER_HPP

#include <QObject>
#include <QString>

#include "automation/automation_types.hpp"
#include "automation/application_launcher.hpp"
#include "automation/desktop_environment_detector.hpp"
#include "llm/ollama_client.hpp"

/// Turns a natural-language desktop request into a structured, reviewed plan.
/// The model output is untrusted: it is parsed strictly, enriched with local
/// environment facts and then validated by AutomationPolicy before approval.
class AutomationPlanner : public QObject {
    Q_OBJECT

public:
    explicit AutomationPlanner(QObject *parent = nullptr);
    ~AutomationPlanner() override = default;

    void setModel(const QString &model);
    [[nodiscard]] QString model() const;
    [[nodiscard]] bool isBusy() const;
    void setEnvironment(const DesktopEnvironmentInfo &environment);
    void setProjectDirectory(const QString &directory);

    void generatePlan(const QString &request);

    [[nodiscard]] static bool isAutomationRequest(const QString &message);
    [[nodiscard]] static QString buildPrompt(const QString &request,
                                             const DesktopEnvironmentInfo &environment,
                                             const QString &projectDirectory,
                                             const QStringList &installedApplications);
    [[nodiscard]] static bool parsePlan(const QString &response,
                                        const QString &request,
                                        AutomationPlan *plan,
                                        QString *error);

signals:
    void planningStarted(const QString &request);
    void planReady(const AutomationPlan &plan);
    void planningFailed(const QString &error);

private slots:
    void onCompletionReceived(const QString &response);
    void onCompletionError(const QString &error);

private:
    void finishWithError(const QString &error);
    void enrichPlan(AutomationPlan *plan);

    OllamaClient          m_client;
    ApplicationLauncher   m_launcher;
    DesktopEnvironmentInfo m_environment;
    QString               m_projectDirectory;
    QString               m_pendingRequest;
    bool                  m_busy{false};
};

#endif // TITANAI_AUTOMATION_PLANNER_HPP
