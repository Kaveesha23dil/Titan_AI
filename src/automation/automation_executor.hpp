#ifndef TITANAI_AUTOMATION_EXECUTOR_HPP
#define TITANAI_AUTOMATION_EXECUTOR_HPP

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>

#include "automation/application_launcher.hpp"
#include "automation/automation_policy.hpp"
#include "automation/automation_types.hpp"
#include "automation/desktop_environment_detector.hpp"
#include "automation/window_manager.hpp"
#include "terminal/terminal_executor.hpp"

/// Executes an approved automation plan one action at a time.
///
/// Execution is bound to the plan fingerprint the user approved. The plan is
/// re-validated immediately before the first action and every action is
/// dispatched without a shell.
class AutomationExecutor : public QObject {
    Q_OBJECT

public:
    explicit AutomationExecutor(QObject *parent = nullptr);
    ~AutomationExecutor() override;

    void setProjectDirectory(const QString &directory);
    void setPolicyContext(const AutomationPolicyContext &context);
    void setEnvironment(const DesktopEnvironmentInfo &environment);

    void setApplicationLauncherForTesting(std::unique_ptr<ApplicationLauncher> launcher);
    void setWindowManagerForTesting(std::unique_ptr<IWindowManager> windowManager);

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] QString currentWorkflowId() const;
    [[nodiscard]] const AutomationWorkflowResult &workflow() const;

    bool execute(const AutomationPlan &plan, const QString &approvedFingerprint,
                 bool userApproved);
    bool retryAction(const AutomationPlan &plan, const QString &actionId,
                     bool userApproved);
    void cancel();

signals:
    void workflowStarted(const QString &workflowId,
                         const AutomationPlan &plan);
    void actionStarted(const QString &actionId, const QString &description);
    void actionFinished(const AutomationActionResult &result);
    void actionOutput(const QString &actionId, const QString &chunk);
    void workflowProgress(int completed, int total);
    void workflowFinished(const AutomationWorkflowResult &result);
    void workflowError(const QString &error);

private slots:
    void processNextAction();
    void onTerminalFinished(int exitCode, QProcess::ExitStatus exitStatus,
                            qint64 durationMs, bool timedOut, bool cancelled);
    void onTerminalRejected(const QString &reason);
    void onTerminalStartFailed(const QString &error);
    void onTerminalStdout(const QString &chunk);
    void onTerminalStderr(const QString &chunk);
    void onWaitTick();

private:
    enum class ActionOutcome { Completed, Failed, Unsupported, Cancelled };

    bool startPlan(const AutomationPlan &plan);
    void dispatchAction(const AutomationAction &action);
    void finishCurrentAction(ActionOutcome outcome, const QString &message,
                             const QString &output = QString());
    void finishWorkflow(AutomationWorkflowStatus status, const QString &error = QString());
    void scheduleNext();
    void markRemainingSkipped(const QString &reason);
    void abortForCancellation();
    void updateProgress();

    [[nodiscard]] QList<WindowInfo> windows(QString *error = nullptr);
    [[nodiscard]] bool findWindow(const AutomationAction &action,
                                  WindowInfo *window, QString *error);
    [[nodiscard]] bool applicationMatchesWindow(const QString &application,
                                                const WindowInfo &window) const;
    [[nodiscard]] bool windowMatchesAction(const AutomationAction &action,
                                           const WindowInfo &window) const;
    [[nodiscard]] bool computeTargetGeometry(const AutomationAction &action,
                                             const WindowInfo &window,
                                             QRect *target, QString *error);
    [[nodiscard]] QString formatWindow(const WindowInfo &window) const;

    std::unique_ptr<ApplicationLauncher> m_launcher;
    std::unique_ptr<IWindowManager> m_windowManager;
    TerminalExecutor m_terminal;
    DesktopEnvironmentInfo m_environment;
    AutomationPolicyContext m_policyContext;
    AutomationWorkflowResult m_workflow;

    QTimer m_waitTimer;
    QElapsedTimer m_waitElapsed;
    AutomationAction m_waitAction;
    QElapsedTimer m_actionElapsed;
    QString m_currentOutput;
    QString m_activeActionId;
    AutomationActionType m_activeType{AutomationActionType::Unknown};
    int m_index{-1};
    bool m_running{false};
    bool m_waiting{false};
    bool m_cancelled{false};
    bool m_requiredFailure{false};
};

#endif // TITANAI_AUTOMATION_EXECUTOR_HPP
