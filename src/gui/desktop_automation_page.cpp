#include "gui/desktop_automation_page.hpp"

#include <QAbstractItemView>
#include <QColor>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include "automation/automation_executor.hpp"
#include "automation/automation_history_manager.hpp"
#include "automation/automation_planner.hpp"

namespace DA {
constexpr auto BgDeep      = "#0b0f19";
constexpr auto BgCard      = "#111827";
constexpr auto BgCardHover = "#1a2236";
constexpr auto BgInput     = "#0f1629";
constexpr auto Border      = "#1e293b";
constexpr auto Accent      = "#6366f1";
constexpr auto AccentGlow  = "#818cf8";
constexpr auto AccentCyan  = "#22d3ee";
constexpr auto Success     = "#22c55e";
constexpr auto TextPrimary = "#f1f5f9";
constexpr auto TextSec     = "#94a3b8";
constexpr auto TextMuted   = "#64748b";
constexpr auto Danger      = "#ef4444";
constexpr auto Warning     = "#f59e0b";
}

namespace {

QString sectionTitleStyle()
{
    return QStringLiteral(
        "QLabel { color: %1; font-size: 12px; font-weight: 700; "
        "letter-spacing: 0.5px; background: transparent; border: none; }")
        .arg(DA::TextSec);
}

QString buttonStyle()
{
    return QStringLiteral(
        "QPushButton { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 7px; padding: 7px 12px; font-size: 12px; font-weight: 600; } "
        "QPushButton:hover { background: %4; border-color: %5; } "
        "QPushButton:disabled { color: %6; background: %7; border-color: %8; }")
        .arg(DA::BgInput, DA::TextPrimary, DA::Border, DA::BgCardHover,
             DA::Accent, DA::TextMuted, DA::BgCard, DA::Border);
}

QString primaryButtonStyle()
{
    return QStringLiteral(
        "QPushButton { background: %1; color: white; border: none; border-radius: 7px; "
        "padding: 8px 14px; font-size: 12px; font-weight: 700; } "
        "QPushButton:hover { background: %2; } "
        "QPushButton:disabled { background: %3; color: %4; }")
        .arg(DA::Accent, DA::AccentGlow, DA::BgInput, DA::TextMuted);
}

QString dangerButtonStyle()
{
    return QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: 1px solid %1; "
        "border-radius: 7px; padding: 7px 12px; font-size: 12px; font-weight: 600; } "
        "QPushButton:hover { background: rgba(239,68,68,0.12); } "
        "QPushButton:disabled { color: %2; border-color: %3; }")
        .arg(DA::Danger, DA::TextMuted, DA::Border);
}

QString actionDetail(const AutomationAction &action)
{
    if (!action.application.isEmpty()) {
        return QStringLiteral("application: %1").arg(action.application);
    }
    if (!action.windowId.isEmpty()) {
        return QStringLiteral("window: %1").arg(action.windowId);
    }
    if (!action.path.isEmpty()) {
        return QStringLiteral("path: %1").arg(action.path);
    }
    if (!action.command.isEmpty()) {
        return QStringLiteral("command: %1 %2")
            .arg(action.command, action.arguments.join(QLatin1Char(' ')))
            .trimmed();
    }
    if (!action.placement.isEmpty()) {
        return QStringLiteral("placement: %1").arg(action.placement);
    }
    return QString();
}

QString entryLabel(const AutomationHistoryEntry &entry)
{
    const QString time = entry.startedAt.isValid()
                             ? entry.startedAt.toString(QStringLiteral("MM-dd HH:mm"))
                             : QStringLiteral("--");
    return QStringLiteral("%1 | %2 | %3")
        .arg(time, automationWorkflowStatusName(entry.status), entry.taskName);
}

} // namespace

DesktopAutomationPage::DesktopAutomationPage(AutomationPlanner *planner,
                                             AutomationExecutor *executor,
                                             AutomationHistoryManager *history,
                                             QWidget *parent)
    : QWidget(parent)
    , m_planner(planner)
    , m_executor(executor)
    , m_history(history)
{
    buildUi();
    applyStyles();

    if (m_planner) {
        connect(m_planner, &AutomationPlanner::planningStarted,
                this, &DesktopAutomationPage::onPlanningStarted);
        connect(m_planner, &AutomationPlanner::planReady,
                this, &DesktopAutomationPage::onPlanReady);
        connect(m_planner, &AutomationPlanner::planningFailed,
                this, &DesktopAutomationPage::onPlanningFailed);
    }

    if (m_executor) {
        connect(m_executor, &AutomationExecutor::workflowStarted,
                this, &DesktopAutomationPage::onWorkflowStarted);
        connect(m_executor, &AutomationExecutor::actionStarted,
                this, &DesktopAutomationPage::onActionStarted);
        connect(m_executor, &AutomationExecutor::actionFinished,
                this, &DesktopAutomationPage::onActionFinished);
        connect(m_executor, &AutomationExecutor::actionOutput,
                this, &DesktopAutomationPage::onActionOutput);
        connect(m_executor, &AutomationExecutor::workflowProgress,
                this, &DesktopAutomationPage::onWorkflowProgress);
        connect(m_executor, &AutomationExecutor::workflowFinished,
                this, &DesktopAutomationPage::onWorkflowFinished);
        connect(m_executor, &AutomationExecutor::workflowError,
                this, &DesktopAutomationPage::onWorkflowError);
    }

    if (m_history) {
        connect(m_history, &AutomationHistoryManager::historyChanged,
                this, &DesktopAutomationPage::refreshHistory);
        refreshHistory();
    }

    setRunning(false);
}

void DesktopAutomationPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(10);

    auto *titleRow = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("Desktop Automation"), this);
    title->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 18px; font-weight: 800; background: transparent; }")
        .arg(DA::TextPrimary));
    titleRow->addWidget(title);
    auto *subtitle = new QLabel(
        QStringLiteral("Describe a desktop task. TitanAI proposes a plan; nothing runs "
                       "until you approve it."),
        this);
    subtitle->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; background: transparent; }")
        .arg(DA::TextMuted));
    titleRow->addSpacing(10);
    titleRow->addWidget(subtitle);
    titleRow->addStretch();
    root->addLayout(titleRow);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    auto *left = new QWidget(splitter);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);

    auto *inputCard = new QFrame(left);
    inputCard->setObjectName(QStringLiteral("automationCard"));
    auto *inputLayout = new QVBoxLayout(inputCard);
    inputLayout->setContentsMargins(14, 12, 14, 12);
    inputLayout->setSpacing(8);

    auto *inputTitle = new QLabel(QStringLiteral("DESKTOP REQUEST"), inputCard);
    inputTitle->setStyleSheet(sectionTitleStyle());
    inputLayout->addWidget(inputTitle);

    m_requestEdit = new QPlainTextEdit(inputCard);
    m_requestEdit->setPlaceholderText(
        QStringLiteral("e.g. Open VS Code on the left and the browser on the right"));
    m_requestEdit->setFixedHeight(62);
    inputLayout->addWidget(m_requestEdit);

    auto *inputRow = new QHBoxLayout;
    m_generateButton = new QPushButton(QStringLiteral("Generate Plan"), inputCard);
    m_generateButton->setStyleSheet(primaryButtonStyle());
    m_generateButton->setCursor(Qt::PointingHandCursor);
    connect(m_generateButton, &QPushButton::clicked,
            this, &DesktopAutomationPage::onGenerateClicked);
    inputRow->addWidget(m_generateButton);

    m_loadingBar = new QProgressBar(inputCard);
    m_loadingBar->setRange(0, 0);
    m_loadingBar->setTextVisible(false);
    m_loadingBar->setFixedHeight(4);
    m_loadingBar->setFixedWidth(100);
    m_loadingBar->setVisible(false);
    inputRow->addWidget(m_loadingBar);

    m_inputStatus = new QLabel(inputCard);
    m_inputStatus->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; background: transparent; }")
        .arg(DA::TextMuted));
    inputRow->addWidget(m_inputStatus, 1);
    inputLayout->addLayout(inputRow);
    leftLayout->addWidget(inputCard);

    auto *planCard = new QFrame(left);
    planCard->setObjectName(QStringLiteral("automationCard"));
    auto *planLayout = new QVBoxLayout(planCard);
    planLayout->setContentsMargins(14, 12, 14, 12);
    planLayout->setSpacing(8);

    auto *planHeader = new QHBoxLayout;
    auto *planTitle = new QLabel(QStringLiteral("PLAN REVIEW"), planCard);
    planTitle->setStyleSheet(sectionTitleStyle());
    planHeader->addWidget(planTitle);
    planHeader->addStretch();
    m_taskLabel = new QLabel(QStringLiteral("No plan generated yet."), planCard);
    m_taskLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 12px; font-weight: 700; background: transparent; }")
        .arg(DA::TextPrimary));
    planHeader->addWidget(m_taskLabel);
    planLayout->addLayout(planHeader);

    m_metaLabel = new QLabel(planCard);
    m_metaLabel->setWordWrap(true);
    m_metaLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_metaLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; background: transparent; }")
        .arg(DA::TextMuted));
    planLayout->addWidget(m_metaLabel);

    m_warningLabel = new QLabel(planCard);
    m_warningLabel->setWordWrap(true);
    m_warningLabel->setVisible(false);
    m_warningLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; background: transparent; }")
        .arg(DA::Warning));
    planLayout->addWidget(m_warningLabel);

    m_actionList = new QListWidget(planCard);
    m_actionList->setSelectionMode(QAbstractItemView::NoSelection);
    m_actionList->setFocusPolicy(Qt::NoFocus);
    m_actionList->setMinimumHeight(120);
    planLayout->addWidget(m_actionList, 1);

    m_progressBar = new QProgressBar(planCard);
    m_progressBar->setRange(0, 1);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(5);
    planLayout->addWidget(m_progressBar);

    m_executionStatus = new QLabel(planCard);
    m_executionStatus->setWordWrap(true);
    m_executionStatus->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; background: transparent; }")
        .arg(DA::TextMuted));
    planLayout->addWidget(m_executionStatus);

    auto *buttonRow = new QHBoxLayout;
    m_approveButton = new QPushButton(QStringLiteral("Approve & Execute"), planCard);
    m_approveButton->setStyleSheet(primaryButtonStyle());
    m_approveButton->setCursor(Qt::PointingHandCursor);
    connect(m_approveButton, &QPushButton::clicked,
            this, &DesktopAutomationPage::onApproveClicked);

    m_cancelPlanButton = new QPushButton(QStringLiteral("Cancel Plan"), planCard);
    m_cancelPlanButton->setStyleSheet(buttonStyle());
    m_cancelPlanButton->setCursor(Qt::PointingHandCursor);
    connect(m_cancelPlanButton, &QPushButton::clicked,
            this, &DesktopAutomationPage::onCancelPlanClicked);

    m_stopButton = new QPushButton(QStringLiteral("Stop Automation"), planCard);
    m_stopButton->setStyleSheet(dangerButtonStyle());
    m_stopButton->setCursor(Qt::PointingHandCursor);
    connect(m_stopButton, &QPushButton::clicked,
            this, &DesktopAutomationPage::onStopClicked);

    m_retryButton = new QPushButton(QStringLiteral("Retry Failed Step"), planCard);
    m_retryButton->setStyleSheet(buttonStyle());
    m_retryButton->setCursor(Qt::PointingHandCursor);
    connect(m_retryButton, &QPushButton::clicked,
            this, &DesktopAutomationPage::onRetryClicked);

    buttonRow->addWidget(m_approveButton);
    buttonRow->addWidget(m_cancelPlanButton);
    buttonRow->addStretch();
    buttonRow->addWidget(m_retryButton);
    buttonRow->addWidget(m_stopButton);
    planLayout->addLayout(buttonRow);

    leftLayout->addWidget(planCard, 1);

    auto *historyCard = new QFrame(splitter);
    historyCard->setObjectName(QStringLiteral("automationCard"));
    auto *historyLayout = new QVBoxLayout(historyCard);
    historyLayout->setContentsMargins(14, 12, 14, 12);
    historyLayout->setSpacing(8);

    auto *historyTitle = new QLabel(QStringLiteral("ACTIVITY HISTORY"), historyCard);
    historyTitle->setStyleSheet(sectionTitleStyle());
    historyLayout->addWidget(historyTitle);

    auto *historyHint = new QLabel(
        QStringLiteral("Local metadata only. Terminal output is never stored."),
        historyCard);
    historyHint->setWordWrap(true);
    historyHint->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 10px; background: transparent; }")
        .arg(DA::TextMuted));
    historyLayout->addWidget(historyHint);

    m_historyList = new QListWidget(historyCard);
    m_historyList->setMinimumWidth(240);
    connect(m_historyList, &QListWidget::itemSelectionChanged,
            this, &DesktopAutomationPage::onHistorySelectionChanged);
    historyLayout->addWidget(m_historyList, 1);

    m_historyStatus = new QLabel(historyCard);
    m_historyStatus->setWordWrap(true);
    m_historyStatus->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 10px; background: transparent; }")
        .arg(DA::TextMuted));
    historyLayout->addWidget(m_historyStatus);

    m_reopenButton = new QPushButton(QStringLiteral("Reopen & Review"), historyCard);
    m_reopenButton->setStyleSheet(buttonStyle());
    m_reopenButton->setCursor(Qt::PointingHandCursor);
    m_reopenButton->setEnabled(false);
    connect(m_reopenButton, &QPushButton::clicked,
            this, &DesktopAutomationPage::onReopenHistoryClicked);
    historyLayout->addWidget(m_reopenButton);

    m_clearHistoryButton = new QPushButton(QStringLiteral("Clear History"), historyCard);
    m_clearHistoryButton->setStyleSheet(dangerButtonStyle());
    m_clearHistoryButton->setCursor(Qt::PointingHandCursor);
    connect(m_clearHistoryButton, &QPushButton::clicked,
            this, &DesktopAutomationPage::onClearHistoryClicked);
    historyLayout->addWidget(m_clearHistoryButton);

    splitter->addWidget(left);
    splitter->addWidget(historyCard);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);
}

void DesktopAutomationPage::applyStyles()
{
    setStyleSheet(QStringLiteral(
        "QWidget { background: %1; } "
        "QFrame#automationCard { background: %2; border: 1px solid %3; border-radius: 10px; } "
        "QPlainTextEdit { background: %4; color: %5; border: 1px solid %3; "
        "border-radius: 7px; padding: 8px; font-size: 13px; } "
        "QPlainTextEdit:focus { border-color: %6; } "
        "QListWidget { background: %4; color: %5; border: 1px solid %3; "
        "border-radius: 7px; font-size: 11px; padding: 4px; } "
        "QListWidget::item { padding: 5px; border-bottom: 1px solid %3; } "
        "QListWidget::item:selected { background: %7; } "
        "QProgressBar { background: %4; border: none; border-radius: 2px; } "
        "QProgressBar::chunk { background: %6; border-radius: 2px; }")
        .arg(DA::BgDeep, DA::BgCard, DA::Border, DA::BgInput, DA::TextPrimary,
             DA::Accent, DA::BgCardHover));
}

void DesktopAutomationPage::setPlan(const AutomationPlan &plan)
{
    m_plan = plan;
    if (m_plan.fingerprint.isEmpty()) {
        m_plan.fingerprint = m_plan.computeFingerprint();
    }
    m_hasPlan = !m_plan.actions.isEmpty();
    m_workflow = AutomationWorkflowResult{};
    m_results.clear();
    for (const AutomationAction &action : m_plan.actions) {
        AutomationActionResult result;
        result.actionId = action.id;
        result.description = action.description;
        result.status = action.supported ? AutomationActionStatus::Pending
                                        : AutomationActionStatus::Unsupported;
        result.message = action.supported ? QString()
                                          : action.unsupportedReason;
        m_results.append(result);
    }

    m_taskLabel->setText(m_plan.taskName.isEmpty() ? QStringLiteral("Desktop task")
                                                   : m_plan.taskName);
    m_metaLabel->setText(
        QStringLiteral("%1 action(s) | Approval required: %2 | Fingerprint: %3")
            .arg(m_plan.actions.size())
            .arg(m_plan.requiresApproval ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(m_plan.fingerprint.left(16)));
    m_warningLabel->setText(m_plan.warnings.join(QStringLiteral("  ")));
    m_warningLabel->setVisible(!m_plan.warnings.isEmpty());

    refreshPlanList();
    m_progressBar->setRange(0, qMax(1, m_plan.actions.size()));
    m_progressBar->setValue(0);
    setRunning(false);
    updateStatus(QStringLiteral("Review the actions, then approve explicitly."),
                 DA::TextSec);
}

void DesktopAutomationPage::showPlanningError(const QString &error)
{
    updateStatus(QStringLiteral("Planning failed: %1").arg(error), DA::Danger);
}

void DesktopAutomationPage::refreshPlanList()
{
    m_actionList->clear();
    for (const AutomationActionResult &result : m_results) {
        const AutomationAction *action = nullptr;
        for (const AutomationAction &candidate : m_plan.actions) {
            if (candidate.id == result.actionId) {
                action = &candidate;
                break;
            }
        }
        const QString typeName = action ? automationActionTypeName(action->type)
                                        : QStringLiteral("action");
        QString text = QStringLiteral("[%1] %2\n    %3")
                           .arg(automationActionStatusName(result.status).toUpper(),
                                result.description.isEmpty() ? typeName
                                                             : result.description,
                                typeName);
        if (action) {
            const QString detail = actionDetail(*action);
            if (!detail.isEmpty()) {
                text += QStringLiteral(" | %1").arg(detail);
            }
            if (!action->supported && !action->unsupportedReason.isEmpty()) {
                text += QStringLiteral("\n    %1").arg(action->unsupportedReason);
            }
        }
        if (!result.message.isEmpty()) {
            text += QStringLiteral("\n    %1").arg(result.message);
        }
        auto *item = new QListWidgetItem(text, m_actionList);
        item->setForeground(QColor(statusColor(result.status)));
        item->setData(Qt::UserRole, result.actionId);
    }
}

void DesktopAutomationPage::refreshHistory()
{
    if (!m_history) {
        return;
    }
    const QString keep = m_selectedHistoryId;
    m_historyList->clear();
    const QList<AutomationHistoryEntry> entries = m_history->allEntries();
    for (const AutomationHistoryEntry &entry : entries) {
        auto *item = new QListWidgetItem(entryLabel(entry), m_historyList);
        item->setData(Qt::UserRole, entry.workflowId);
        item->setForeground(QColor(entry.status == AutomationWorkflowStatus::Failed
                                       ? DA::Danger
                                       : entry.status == AutomationWorkflowStatus::Completed
                                             ? DA::Success
                                             : DA::TextSec));
        if (entry.workflowId == keep) {
            m_historyList->setCurrentItem(item);
        }
    }
    m_clearHistoryButton->setEnabled(!entries.isEmpty());
    m_historyStatus->setText(entries.isEmpty()
                                 ? QStringLiteral("No automation history yet.")
                                 : QStringLiteral("%1 saved plan(s).").arg(entries.size()));
    onHistorySelectionChanged();
}

void DesktopAutomationPage::updateActionItem(const AutomationActionResult &result)
{
    for (int i = 0; i < m_results.size(); ++i) {
        if (m_results[i].actionId == result.actionId) {
            m_results[i] = result;
            break;
        }
    }
    refreshPlanList();
}

void DesktopAutomationPage::setBusy(bool busy)
{
    m_generateButton->setEnabled(!busy);
    m_loadingBar->setVisible(busy);
    if (busy) {
        m_inputStatus->setText(QStringLiteral("Planning locally..."));
    }
}

void DesktopAutomationPage::setRunning(bool running)
{
    m_approveButton->setEnabled(m_hasPlan && !running);
    m_cancelPlanButton->setEnabled(m_hasPlan && !running);
    m_generateButton->setEnabled(!running);
    m_stopButton->setEnabled(running);
    bool retry = false;
    for (const AutomationActionResult &result : m_results) {
        if (result.status == AutomationActionStatus::Failed) {
            retry = true;
            break;
        }
    }
    m_retryButton->setEnabled(!running && retry && m_hasPlan);
}

void DesktopAutomationPage::updateStatus(const QString &text, const QString &color)
{
    m_executionStatus->setText(text);
    m_executionStatus->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 11px; background: transparent; }")
            .arg(color));
    emit statusMessage(text);
}

QString DesktopAutomationPage::statusColor(AutomationActionStatus status)
{
    switch (status) {
    case AutomationActionStatus::Completed:  return DA::Success;
    case AutomationActionStatus::Failed:     return DA::Danger;
    case AutomationActionStatus::Cancelled:
    case AutomationActionStatus::Unsupported:
    case AutomationActionStatus::Skipped:    return DA::Warning;
    case AutomationActionStatus::Running:    return DA::AccentGlow;
    case AutomationActionStatus::Pending:
    case AutomationActionStatus::WaitingForApproval: return DA::TextMuted;
    }
    return DA::TextMuted;
}

void DesktopAutomationPage::onGenerateClicked()
{
    if (!m_planner) {
        return;
    }
    const QString request = m_requestEdit->toPlainText().trimmed();
    if (request.isEmpty()) {
        updateStatus(QStringLiteral("Enter a desktop request first."), DA::Warning);
        return;
    }
    if (m_planner->isBusy()) {
        updateStatus(QStringLiteral("A plan is already being generated."), DA::Warning);
        return;
    }
    m_planner->generatePlan(request);
}

void DesktopAutomationPage::onApproveClicked()
{
    if (!m_executor || !m_hasPlan) {
        return;
    }
    if (m_executor->isRunning()) {
        updateStatus(QStringLiteral("A plan is already running."), DA::Warning);
        return;
    }

    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Approve Desktop Automation"));
    box.setIcon(QMessageBox::Question);
    box.setText(QStringLiteral("Run this automation plan?"));
    box.setInformativeText(
        QStringLiteral("%1 action(s) will run. Nothing runs without this confirmation.")
            .arg(m_plan.actions.size()));
    box.setDetailedText(m_plan.taskName + QStringLiteral("\n\n")
                        + QStringLiteral("Fingerprint: ") + m_plan.fingerprint);
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Yes) {
        updateStatus(QStringLiteral("Execution cancelled before starting."), DA::TextMuted);
        return;
    }

    m_plan.fingerprint = m_plan.computeFingerprint();
    if (!m_executor->execute(m_plan, m_plan.fingerprint, true)) {
        setRunning(false);
    }
}

void DesktopAutomationPage::onCancelPlanClicked()
{
    if (m_executor && m_executor->isRunning()) {
        return;
    }
    m_plan = AutomationPlan{};
    m_results.clear();
    m_hasPlan = false;
    m_taskLabel->setText(QStringLiteral("No plan generated yet."));
    m_metaLabel->clear();
    m_warningLabel->clear();
    m_warningLabel->setVisible(false);
    m_actionList->clear();
    m_progressBar->setRange(0, 1);
    m_progressBar->setValue(0);
    setRunning(false);
    updateStatus(QStringLiteral("Plan discarded."), DA::TextMuted);
}

void DesktopAutomationPage::onStopClicked()
{
    if (m_executor && m_executor->isRunning()) {
        m_executor->cancel();
    }
}

void DesktopAutomationPage::onRetryClicked()
{
    if (!m_executor || !m_hasPlan) {
        return;
    }
    QString actionId;
    for (const AutomationActionResult &result : m_results) {
        if (result.status == AutomationActionStatus::Failed) {
            actionId = result.actionId;
            break;
        }
    }
    if (actionId.isEmpty()) {
        updateStatus(QStringLiteral("No failed action to retry."), DA::TextMuted);
        return;
    }

    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Retry Failed Step"));
    box.setIcon(QMessageBox::Question);
    box.setText(QStringLiteral("Retry the failed step?"));
    box.setInformativeText(
        QStringLiteral("The step will be re-validated and needs a fresh confirmation."));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Yes) {
        return;
    }

    m_executor->retryAction(m_plan, actionId, true);
}

void DesktopAutomationPage::onReopenHistoryClicked()
{
    if (!m_history || m_selectedHistoryId.isEmpty()) {
        return;
    }
    const QList<AutomationHistoryEntry> entries = m_history->allEntries();
    for (const AutomationHistoryEntry &entry : entries) {
        if (entry.workflowId == m_selectedHistoryId) {
            setPlan(entry.plan);
            updateStatus(
                QStringLiteral("Reopened '%1'. Review it and approve again to run.")
                    .arg(entry.taskName),
                DA::Warning);
            return;
        }
    }
}

void DesktopAutomationPage::onClearHistoryClicked()
{
    if (!m_history || m_history->count() == 0) {
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("Clear Automation History"),
                              QStringLiteral("Delete all saved automation plans?"),
                              QMessageBox::Yes | QMessageBox::Cancel,
                              QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    m_history->clear();
    m_selectedHistoryId.clear();
    refreshHistory();
}

void DesktopAutomationPage::onHistorySelectionChanged()
{
    const QListWidgetItem *item = m_historyList->currentItem();
    const bool hasSelection = item != nullptr;
    m_reopenButton->setEnabled(hasSelection);
    m_selectedHistoryId = hasSelection ? item->data(Qt::UserRole).toString() : QString();
    if (hasSelection) {
        m_historyStatus->setText(
            QStringLiteral("Select Reopen & Review to load this plan for a new approval."));
    }
}

void DesktopAutomationPage::onPlanningStarted(const QString &)
{
    setBusy(true);
    updateStatus(QStringLiteral("Generating a desktop plan locally..."), DA::AccentGlow);
}

void DesktopAutomationPage::onPlanReady(const AutomationPlan &plan)
{
    setBusy(false);
    m_inputStatus->clear();
    setPlan(plan);
    updateStatus(QStringLiteral("Plan ready. Review every action before approving."),
                 DA::Success);
}

void DesktopAutomationPage::onPlanningFailed(const QString &error)
{
    setBusy(false);
    m_inputStatus->clear();
    updateStatus(QStringLiteral("Planning failed: %1").arg(error), DA::Danger);
}

void DesktopAutomationPage::onWorkflowStarted(const QString &,
                                              const AutomationPlan &plan)
{
    m_plan = plan;
    m_hasPlan = !plan.actions.isEmpty();
    m_workflow = m_executor ? m_executor->workflow() : AutomationWorkflowResult{};
    m_results = m_workflow.results;
    refreshPlanList();
    m_progressBar->setRange(0, qMax(1, m_results.size()));
    m_progressBar->setValue(0);
    setRunning(true);
    updateStatus(QStringLiteral("Running automation..."), DA::AccentGlow);
}

void DesktopAutomationPage::onActionStarted(const QString &actionId,
                                            const QString &description)
{
    for (AutomationActionResult &result : m_results) {
        if (result.actionId == actionId) {
            result.status = AutomationActionStatus::Running;
            result.message = description;
            break;
        }
    }
    refreshPlanList();
}

void DesktopAutomationPage::onActionFinished(const AutomationActionResult &result)
{
    updateActionItem(result);
    setRunning(m_executor && m_executor->isRunning());
}

void DesktopAutomationPage::onActionOutput(const QString &, const QString &chunk)
{
    if (chunk.trimmed().isEmpty()) {
        return;
    }
    QString line = chunk.trimmed().split(QLatin1Char('\n')).last();
    if (line.size() > 120) {
        line = line.left(120) + QStringLiteral("...");
    }
    updateStatus(line, DA::TextSec);
}

void DesktopAutomationPage::onWorkflowProgress(int completed, int total)
{
    m_progressBar->setRange(0, qMax(1, total));
    m_progressBar->setValue(completed);
}

void DesktopAutomationPage::onWorkflowFinished(const AutomationWorkflowResult &result)
{
    m_workflow = result;
    m_results = result.results;
    refreshPlanList();
    setRunning(false);

    QString text = QStringLiteral("Workflow %1.")
                       .arg(automationWorkflowStatusName(result.status));
    if (!result.error.isEmpty()) {
        text += QStringLiteral(" %1").arg(result.error);
    }
    QString color = DA::TextSec;
    switch (result.status) {
    case AutomationWorkflowStatus::Completed: color = DA::Success; break;
    case AutomationWorkflowStatus::Failed:    color = DA::Danger; break;
    case AutomationWorkflowStatus::Cancelled: color = DA::Warning; break;
    default: break;
    }
    updateStatus(text, color);
}

void DesktopAutomationPage::onWorkflowError(const QString &error)
{
    updateStatus(QStringLiteral("Execution rejected: %1").arg(error), DA::Danger);
    if (!m_executor || !m_executor->isRunning()) {
        setRunning(false);
    }
}
