#ifndef TITANAI_DESKTOP_AUTOMATION_PAGE_HPP
#define TITANAI_DESKTOP_AUTOMATION_PAGE_HPP

#include <QWidget>

#include "automation/automation_types.hpp"

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

class AutomationPlanner;
class AutomationExecutor;
class AutomationHistoryManager;

/// Dedicated Desktop Automation page: request input, reviewed plan, explicit
/// approval, live execution status and local plan history.
class DesktopAutomationPage : public QWidget {
    Q_OBJECT

public:
    explicit DesktopAutomationPage(AutomationPlanner *planner,
                                   AutomationExecutor *executor,
                                   AutomationHistoryManager *history,
                                   QWidget *parent = nullptr);
    ~DesktopAutomationPage() override = default;

    void setPlan(const AutomationPlan &plan);
    void showPlanningError(const QString &error);

signals:
    void statusMessage(const QString &message);

private slots:
    void onGenerateClicked();
    void onApproveClicked();
    void onCancelPlanClicked();
    void onStopClicked();
    void onRetryClicked();
    void onReopenHistoryClicked();
    void onClearHistoryClicked();
    void onHistorySelectionChanged();

    void onPlanningStarted(const QString &request);
    void onPlanReady(const AutomationPlan &plan);
    void onPlanningFailed(const QString &error);

    void onWorkflowStarted(const QString &workflowId, const AutomationPlan &plan);
    void onActionStarted(const QString &actionId, const QString &description);
    void onActionFinished(const AutomationActionResult &result);
    void onActionOutput(const QString &actionId, const QString &chunk);
    void onWorkflowProgress(int completed, int total);
    void onWorkflowFinished(const AutomationWorkflowResult &result);
    void onWorkflowError(const QString &error);

private:
    void buildUi();
    void applyStyles();
    void refreshPlanList();
    void refreshHistory();
    void updateActionItem(const AutomationActionResult &result);
    void setBusy(bool busy);
    void setRunning(bool running);
    void updateStatus(const QString &text, const QString &color);
    static QString statusColor(AutomationActionStatus status);

    AutomationPlanner        *m_planner{nullptr};
    AutomationExecutor       *m_executor{nullptr};
    AutomationHistoryManager *m_history{nullptr};

    AutomationPlan                 m_plan;
    AutomationWorkflowResult       m_workflow;
    QList<AutomationActionResult>  m_results;
    QString                        m_selectedHistoryId;
    bool                           m_hasPlan{false};

    QPlainTextEdit *m_requestEdit{nullptr};
    QPushButton    *m_generateButton{nullptr};
    QProgressBar   *m_loadingBar{nullptr};
    QLabel         *m_inputStatus{nullptr};

    QLabel         *m_taskLabel{nullptr};
    QLabel         *m_metaLabel{nullptr};
    QLabel         *m_warningLabel{nullptr};
    QListWidget    *m_actionList{nullptr};
    QProgressBar   *m_progressBar{nullptr};
    QLabel         *m_executionStatus{nullptr};
    QPushButton    *m_approveButton{nullptr};
    QPushButton    *m_cancelPlanButton{nullptr};
    QPushButton    *m_stopButton{nullptr};
    QPushButton    *m_retryButton{nullptr};

    QListWidget *m_historyList{nullptr};
    QPushButton *m_reopenButton{nullptr};
    QPushButton *m_clearHistoryButton{nullptr};
    QLabel      *m_historyStatus{nullptr};
};

#endif // TITANAI_DESKTOP_AUTOMATION_PAGE_HPP
