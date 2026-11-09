#include "automation/automation_executor.hpp"

#include <QDir>
#include <QFileInfo>
#include <QUuid>

#include <algorithm>

#include "terminal/terminal_types.hpp"

namespace {

constexpr int kMaxActionOutput = 32 * 1024;

} // namespace

AutomationExecutor::AutomationExecutor(QObject *parent)
    : QObject(parent)
    , m_launcher(std::make_unique<ApplicationLauncher>())
    , m_environment(DesktopEnvironmentDetector::detect())
    , m_policyContext(AutomationPolicy::contextFor(QString()))
{
    m_windowManager = createWindowManager(m_environment);

    m_waitTimer.setSingleShot(true);
    connect(&m_waitTimer, &QTimer::timeout, this, &AutomationExecutor::onWaitTick);

    connect(&m_terminal, &TerminalExecutor::finished,
            this, &AutomationExecutor::onTerminalFinished);
    connect(&m_terminal, &TerminalExecutor::executionRejected,
            this, &AutomationExecutor::onTerminalRejected);
    connect(&m_terminal, &TerminalExecutor::failedToStart,
            this, &AutomationExecutor::onTerminalStartFailed);
    connect(&m_terminal, &TerminalExecutor::standardOutput,
            this, &AutomationExecutor::onTerminalStdout);
    connect(&m_terminal, &TerminalExecutor::standardError,
            this, &AutomationExecutor::onTerminalStderr);
}

AutomationExecutor::~AutomationExecutor()
{
    m_waitTimer.stop();
    if (m_terminal.isRunning()) {
        m_terminal.cancel();
    }
}

void AutomationExecutor::setProjectDirectory(const QString &directory)
{
    m_policyContext = AutomationPolicy::contextFor(directory);
}

void AutomationExecutor::setPolicyContext(const AutomationPolicyContext &context)
{
    m_policyContext = context;
}

void AutomationExecutor::setEnvironment(const DesktopEnvironmentInfo &environment)
{
    m_environment = environment;
    m_windowManager = createWindowManager(m_environment);
}

void AutomationExecutor::setApplicationLauncherForTesting(
    std::unique_ptr<ApplicationLauncher> launcher)
{
    if (launcher) {
        m_launcher = std::move(launcher);
    }
}

void AutomationExecutor::setWindowManagerForTesting(
    std::unique_ptr<IWindowManager> windowManager)
{
    if (windowManager) {
        m_windowManager = std::move(windowManager);
    }
}

bool AutomationExecutor::isRunning() const
{
    return m_running;
}

QString AutomationExecutor::currentWorkflowId() const
{
    return m_workflow.workflowId;
}

const AutomationWorkflowResult &AutomationExecutor::workflow() const
{
    return m_workflow;
}

bool AutomationExecutor::execute(const AutomationPlan &plan,
                                 const QString &approvedFingerprint,
                                 bool userApproved)
{
    if (m_running) {
        emit workflowError(QStringLiteral("Another automation plan is already running."));
        return false;
    }
    if (!userApproved) {
        emit workflowError(QStringLiteral("Explicit user approval is required."));
        return false;
    }
    if (!plan.requiresApproval) {
        emit workflowError(QStringLiteral("The plan does not require approval."));
        return false;
    }

    const QString expectedFingerprint = plan.computeFingerprint();
    if (approvedFingerprint.isEmpty() || approvedFingerprint != expectedFingerprint) {
        emit workflowError(
            QStringLiteral("Approval does not match the current plan. Review it again."));
        return false;
    }

    const AutomationPolicyResult policy =
        AutomationPolicy::validatePlan(plan, m_policyContext);
    if (!policy.allowed) {
        emit workflowError(QStringLiteral("Plan rejected by safety policy: %1")
                               .arg(policy.reason));
        return false;
    }

    return startPlan(plan);
}

bool AutomationExecutor::retryAction(const AutomationPlan &plan, const QString &actionId,
                                     bool userApproved)
{
    if (!userApproved) {
        emit workflowError(QStringLiteral("Retrying a step requires confirmation."));
        return false;
    }

    const auto it = std::find_if(plan.actions.cbegin(), plan.actions.cend(),
                                 [&](const AutomationAction &action) {
                                     return action.id == actionId;
                                 });
    if (it == plan.actions.cend()) {
        emit workflowError(QStringLiteral("The action to retry was not found."));
        return false;
    }

    AutomationPlan retry;
    retry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    retry.taskName = QStringLiteral("Retry: %1").arg(plan.taskName);
    retry.request = plan.request;
    retry.requiresApproval = true;
    retry.actions.append(*it);
    retry.fingerprint = retry.computeFingerprint();
    return execute(retry, retry.fingerprint, true);
}

void AutomationExecutor::cancel()
{
    if (!m_running) {
        return;
    }
    m_cancelled = true;
    m_waitTimer.stop();
    m_waiting = false;
    if (m_terminal.isRunning()) {
        m_terminal.cancel();
    }
    abortForCancellation();
}

bool AutomationExecutor::startPlan(const AutomationPlan &plan)
{
    m_running = true;
    m_cancelled = false;
    m_waiting = false;
    m_requiredFailure = false;
    m_index = -1;
    m_currentOutput.clear();
    m_activeActionId.clear();
    m_activeType = AutomationActionType::Unknown;

    m_workflow = AutomationWorkflowResult{};
    m_workflow.workflowId = plan.id;
    m_workflow.plan = plan;
    m_workflow.status = AutomationWorkflowStatus::Running;
    m_workflow.startedAt = QDateTime::currentDateTime();
    m_workflow.results.clear();
    for (const AutomationAction &action : plan.actions) {
        AutomationActionResult result;
        result.actionId = action.id;
        result.description = action.description;
        result.status = AutomationActionStatus::Pending;
        m_workflow.results.append(result);
    }

    emit workflowStarted(m_workflow.workflowId, plan);
    QTimer::singleShot(0, this, &AutomationExecutor::processNextAction);
    return true;
}

void AutomationExecutor::processNextAction()
{
    if (!m_running) {
        return;
    }
    if (m_cancelled) {
        abortForCancellation();
        return;
    }

    ++m_index;
    if (m_index >= m_workflow.plan.actions.size()) {
        finishWorkflow(AutomationWorkflowStatus::Completed);
        return;
    }

    const AutomationAction &action = m_workflow.plan.actions.at(m_index);
    m_activeActionId = action.id;
    m_activeType = action.type;
    m_currentOutput.clear();
    m_actionElapsed.start();

    if (!action.supported) {
        AutomationActionResult result;
        result.actionId = action.id;
        result.description = action.description;
        result.status = AutomationActionStatus::Unsupported;
        result.message = action.unsupportedReason.isEmpty()
            ? QStringLiteral("This action is not supported on this desktop.")
            : action.unsupportedReason;
        m_workflow.results[m_index] = result;
        emit actionFinished(result);
        updateProgress();
        scheduleNext();
        return;
    }

    m_workflow.results[m_index].status = AutomationActionStatus::Running;
    emit actionStarted(action.id, action.description);
    dispatchAction(action);
}

void AutomationExecutor::dispatchAction(const AutomationAction &action)
{
    QString error;

    switch (action.type) {
    case AutomationActionType::LaunchApplication: {
        const ApplicationInfo app = m_launcher->resolve(action.application, &error);
        if (!app.isValid()) {
            finishCurrentAction(
                ActionOutcome::Failed,
                error.isEmpty()
                    ? QStringLiteral("Application '%1' was not found.").arg(action.application)
                    : error);
            return;
        }
        if (!m_launcher->launch(app, {}, m_policyContext.projectRoot, &error)) {
            finishCurrentAction(ActionOutcome::Failed, error);
            return;
        }
        finishCurrentAction(ActionOutcome::Completed,
                            QStringLiteral("Launched %1.").arg(app.name));
        return;
    }

    case AutomationActionType::FocusWindow: {
        WindowInfo window;
        if (!findWindow(action, &window, &error)) {
            finishCurrentAction(ActionOutcome::Failed, error);
            return;
        }
        if (!m_windowManager->focusWindow(window.id, &error)) {
            finishCurrentAction(ActionOutcome::Failed, error);
            return;
        }
        finishCurrentAction(ActionOutcome::Completed,
                            QStringLiteral("Focused %1.").arg(formatWindow(window)));
        return;
    }

    case AutomationActionType::ListWindows: {
        const QList<WindowInfo> all = windows(&error);
        if (!error.isEmpty()) {
            finishCurrentAction(ActionOutcome::Failed, error);
            return;
        }
        QStringList lines;
        for (const WindowInfo &window : all) {
            if (!action.application.isEmpty()
                && !applicationMatchesWindow(action.application, window)) {
                continue;
            }
            lines.append(formatWindow(window));
        }
        const QString output = lines.isEmpty() ? QStringLiteral("No windows found.")
                                               : lines.join(QLatin1Char('\n'));
        finishCurrentAction(ActionOutcome::Completed,
                            QStringLiteral("Listed %1 window(s).").arg(lines.size()),
                            output);
        return;
    }

    case AutomationActionType::GetActiveWindow: {
        const WindowInfo window = m_windowManager->activeWindow(&error);
        if (!error.isEmpty() || window.id.isEmpty()) {
            finishCurrentAction(ActionOutcome::Failed,
                                error.isEmpty()
                                    ? QStringLiteral("No active window was found.")
                                    : error);
            return;
        }
        finishCurrentAction(ActionOutcome::Completed,
                            QStringLiteral("Active window: %1").arg(formatWindow(window)),
                            formatWindow(window));
        return;
    }

    case AutomationActionType::MoveWindow: {
        WindowInfo window;
        if (!findWindow(action, &window, &error)) {
            finishCurrentAction(ActionOutcome::Failed, error);
            return;
        }
        QRect target;
        if (!computeTargetGeometry(action, window, &target, &error)) {
            finishCurrentAction(ActionOutcome::Failed, error);
            return;
        }
        if (!m_windowManager->moveWindow(window.id, target, &error)) {
            finishCurrentAction(ActionOutcome::Failed, error);
            return;
        }
        finishCurrentAction(
            ActionOutcome::Completed,
            QStringLiteral("Moved %1 to %2,%3 (%4x%5).")
                .arg(window.id)
                .arg(target.x())
                .arg(target.y())
                .arg(target.width())
                .arg(target.height()));
        return;
    }

    case AutomationActionType::ResizeWindow: {
        WindowInfo window;
        if (!findWindow(action, &window, &error)) {
            finishCurrentAction(ActionOutcome::Failed, error);
            return;
        }
        if (!m_windowManager->resizeWindow(window.id,
                                           QSize(action.width, action.height),
                                           &error)) {
            finishCurrentAction(ActionOutcome::Failed, error);
            return;
        }
        finishCurrentAction(ActionOutcome::Completed,
                            QStringLiteral("Resized %1 to %2x%3.")
                                .arg(window.id)
                                .arg(action.width)
                                .arg(action.height));
        return;
    }

    case AutomationActionType::OpenDirectory:
    case AutomationActionType::OpenFile: {
        const AutomationPolicyResult check =
            AutomationPolicy::validateAction(action, m_policyContext);
        if (!check.allowed || check.resolvedPath.isEmpty()) {
            finishCurrentAction(ActionOutcome::Failed,
                                check.reason.isEmpty()
                                    ? QStringLiteral("The path is not authorized.")
                                    : check.reason);
            return;
        }
        const bool directory = action.type == AutomationActionType::OpenDirectory;
        const bool ok = directory
            ? m_launcher->openDirectory(check.resolvedPath, &error)
            : m_launcher->openFile(check.resolvedPath, &error);
        if (!ok) {
            finishCurrentAction(ActionOutcome::Failed, error);
            return;
        }
        finishCurrentAction(
            ActionOutcome::Completed,
            QStringLiteral("%1 %2.").arg(directory ? QStringLiteral("Opened directory")
                                                   : QStringLiteral("Opened file"),
                                         check.resolvedPath));
        return;
    }

    case AutomationActionType::WaitForWindow:
        m_waitAction = action;
        m_waiting = true;
        m_waitElapsed.start();
        onWaitTick();
        return;

    case AutomationActionType::RunApprovedTerminalCommand: {
        TerminalCommandProposal proposal;
        proposal.command = action.command;
        proposal.arguments = action.arguments;
        proposal.explanation = action.description;
        proposal.modelRisk = TerminalRisk::Low;
        proposal.requiresSudo = false;
        proposal.workingDirectory = m_policyContext.projectRoot.isEmpty()
                                        ? QDir::homePath()
                                        : m_policyContext.projectRoot;

        m_terminal.setTimeoutMs(qBound(1000, action.timeoutMs, 120000));
        m_terminal.execute(proposal, true);
        return;
    }

    case AutomationActionType::Unknown:
        finishCurrentAction(ActionOutcome::Failed,
                            QStringLiteral("Unknown action type."));
        return;
    }
}

void AutomationExecutor::onWaitTick()
{
    if (!m_running || !m_waiting) {
        return;
    }

    QString error;
    const QList<WindowInfo> all = windows(&error);
    if (!error.isEmpty()) {
        m_waiting = false;
        finishCurrentAction(ActionOutcome::Failed, error);
        return;
    }

    for (const WindowInfo &window : all) {
        if (windowMatchesAction(m_waitAction, window)) {
            m_waiting = false;
            finishCurrentAction(ActionOutcome::Completed,
                                QStringLiteral("Window appeared: %1")
                                    .arg(formatWindow(window)),
                                formatWindow(window));
            return;
        }
    }

    const int timeout = qBound(100, m_waitAction.timeoutMs, 30000);
    if (m_waitElapsed.elapsed() >= timeout) {
        m_waiting = false;
        const QString target = m_waitAction.application.isEmpty()
                                   ? m_waitAction.windowId
                                   : m_waitAction.application;
        finishCurrentAction(
            ActionOutcome::Failed,
            QStringLiteral("Timed out waiting for window '%1'.").arg(target));
        return;
    }

    m_waitTimer.start(250);
}

void AutomationExecutor::finishCurrentAction(ActionOutcome outcome,
                                             const QString &message,
                                             const QString &output)
{
    if (m_index < 0 || m_index >= m_workflow.results.size()) {
        return;
    }

    AutomationActionResult &result = m_workflow.results[m_index];
    result.message = message;
    result.output = output;
    if (result.output.isEmpty() && !m_currentOutput.isEmpty()) {
        result.output = m_currentOutput;
    }
    if (result.output.size() > kMaxActionOutput) {
        result.output = result.output.left(kMaxActionOutput)
                        + QStringLiteral("\n[output truncated]");
    }
    result.durationMs = m_actionElapsed.isValid() ? m_actionElapsed.elapsed() : 0;

    switch (outcome) {
    case ActionOutcome::Completed:
        result.status = AutomationActionStatus::Completed;
        break;
    case ActionOutcome::Failed:
        result.status = AutomationActionStatus::Failed;
        break;
    case ActionOutcome::Unsupported:
        result.status = AutomationActionStatus::Unsupported;
        break;
    case ActionOutcome::Cancelled:
        result.status = AutomationActionStatus::Cancelled;
        break;
    }

    emit actionFinished(result);
    m_activeActionId.clear();
    m_activeType = AutomationActionType::Unknown;
    updateProgress();

    if (m_cancelled || outcome == ActionOutcome::Cancelled) {
        return;
    }

    const AutomationAction &action = m_workflow.plan.actions.at(m_index);
    if (outcome == ActionOutcome::Failed && action.required) {
        m_requiredFailure = true;
        markRemainingSkipped(QStringLiteral(
            "Skipped because a required action failed."));
        finishWorkflow(AutomationWorkflowStatus::Failed, message);
        return;
    }

    scheduleNext();
}

void AutomationExecutor::scheduleNext()
{
    if (!m_running || m_cancelled) {
        return;
    }
    QTimer::singleShot(0, this, &AutomationExecutor::processNextAction);
}

void AutomationExecutor::markRemainingSkipped(const QString &reason)
{
    for (int i = m_index + 1; i < m_workflow.results.size(); ++i) {
        AutomationActionResult &result = m_workflow.results[i];
        if (result.status != AutomationActionStatus::Pending) {
            continue;
        }
        result.status = AutomationActionStatus::Skipped;
        result.message = reason;
        emit actionFinished(result);
    }
    updateProgress();
}

void AutomationExecutor::updateProgress()
{
    int completed = 0;
    for (const AutomationActionResult &result : m_workflow.results) {
        switch (result.status) {
        case AutomationActionStatus::Completed:
        case AutomationActionStatus::Failed:
        case AutomationActionStatus::Unsupported:
        case AutomationActionStatus::Cancelled:
        case AutomationActionStatus::Skipped:
            ++completed;
            break;
        case AutomationActionStatus::Pending:
        case AutomationActionStatus::WaitingForApproval:
        case AutomationActionStatus::Running:
            break;
        }
    }
    emit workflowProgress(completed, m_workflow.results.size());
}

void AutomationExecutor::finishWorkflow(AutomationWorkflowStatus status,
                                        const QString &error)
{
    m_running = false;
    m_waiting = false;
    m_waitTimer.stop();
    m_workflow.status = status;
    m_workflow.error = error;
    m_workflow.finishedAt = QDateTime::currentDateTime();
    emit workflowProgress(m_workflow.results.size(), m_workflow.results.size());
    emit workflowFinished(m_workflow);
}

void AutomationExecutor::abortForCancellation()
{
    if (!m_running) {
        return;
    }
    if (m_index >= 0 && m_index < m_workflow.results.size()
        && m_workflow.results[m_index].status == AutomationActionStatus::Running) {
        AutomationActionResult &result = m_workflow.results[m_index];
        result.status = AutomationActionStatus::Cancelled;
        result.message = QStringLiteral("Cancelled by user.");
        result.durationMs = m_actionElapsed.isValid() ? m_actionElapsed.elapsed() : 0;
        if (!m_currentOutput.isEmpty()) {
            result.output = m_currentOutput;
        }
        emit actionFinished(result);
    }
    markRemainingSkipped(QStringLiteral("Cancelled by user."));
    finishWorkflow(AutomationWorkflowStatus::Cancelled,
                   QStringLiteral("Automation cancelled by user."));
}

void AutomationExecutor::onTerminalFinished(int exitCode,
                                            QProcess::ExitStatus exitStatus,
                                            qint64,
                                            bool timedOut, bool cancelled)
{
    if (!m_running || m_activeType != AutomationActionType::RunApprovedTerminalCommand
        || m_cancelled) {
        return;
    }
    if (cancelled) {
        return;
    }
    if (timedOut) {
        finishCurrentAction(ActionOutcome::Failed,
                            QStringLiteral("Terminal command timed out."));
        return;
    }
    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        finishCurrentAction(ActionOutcome::Completed,
                            QStringLiteral("Command completed successfully."));
        return;
    }
    finishCurrentAction(
        ActionOutcome::Failed,
        QStringLiteral("Command failed with exit code %1.").arg(exitCode));
}

void AutomationExecutor::onTerminalRejected(const QString &reason)
{
    if (!m_running || m_activeType != AutomationActionType::RunApprovedTerminalCommand
        || m_cancelled) {
        return;
    }
    finishCurrentAction(ActionOutcome::Failed, reason);
}

void AutomationExecutor::onTerminalStartFailed(const QString &error)
{
    if (!m_running || m_activeType != AutomationActionType::RunApprovedTerminalCommand
        || m_cancelled) {
        return;
    }
    finishCurrentAction(ActionOutcome::Failed, error);
}

void AutomationExecutor::onTerminalStdout(const QString &chunk)
{
    if (m_cancelled) {
        return;
    }
    m_currentOutput += chunk;
    emit actionOutput(m_activeActionId, chunk);
}

void AutomationExecutor::onTerminalStderr(const QString &chunk)
{
    if (m_cancelled) {
        return;
    }
    m_currentOutput += chunk;
    emit actionOutput(m_activeActionId, chunk);
}

QList<WindowInfo> AutomationExecutor::windows(QString *error)
{
    if (!m_windowManager) {
        if (error) *error = QStringLiteral("No window manager is available.");
        return {};
    }
    return m_windowManager->listWindows(error);
}

bool AutomationExecutor::findWindow(const AutomationAction &action,
                                    WindowInfo *window, QString *error)
{
    const QList<WindowInfo> all = windows(error);
    if (error && !error->isEmpty()) {
        return false;
    }

    if (!action.windowId.isEmpty()) {
        for (const WindowInfo &candidate : all) {
            if (candidate.id.compare(action.windowId, Qt::CaseInsensitive) == 0) {
                if (window) *window = candidate;
                return true;
            }
        }
        if (error) {
            *error = QStringLiteral("Window '%1' was not found.").arg(action.windowId);
        }
        return false;
    }

    for (const WindowInfo &candidate : all) {
        if (applicationMatchesWindow(action.application, candidate)) {
            if (window) *window = candidate;
            return true;
        }
    }

    if (error) {
        *error = QStringLiteral("No open window matched application '%1'.")
                     .arg(action.application);
    }
    return false;
}

bool AutomationExecutor::windowMatchesAction(const AutomationAction &action,
                                             const WindowInfo &window) const
{
    if (!action.windowId.isEmpty()) {
        return window.id.compare(action.windowId, Qt::CaseInsensitive) == 0;
    }
    return applicationMatchesWindow(action.application, window);
}

bool AutomationExecutor::applicationMatchesWindow(const QString &application,
                                                  const WindowInfo &window) const
{
    const QString needle = application.trimmed().toLower();
    if (needle.isEmpty()) {
        return false;
    }

    auto matches = [&](const QString &candidate) {
        return !candidate.isEmpty() && candidate.toLower() == needle;
    };

    if (matches(window.appClass) || matches(window.appName)
        || matches(window.title)) {
        return true;
    }
    if (window.title.toLower().contains(needle)
        || window.appClass.toLower().contains(needle)) {
        return true;
    }

    QString error;
    const ApplicationInfo info = m_launcher->resolve(application, &error);
    if (info.isValid()) {
        if (matches(info.id) || matches(info.name)
            || matches(QFileInfo(info.executable).fileName())) {
            return true;
        }
    }
    return false;
}

bool AutomationExecutor::computeTargetGeometry(const AutomationAction &action,
                                               const WindowInfo &window,
                                               QRect *target, QString *error)
{
    if (action.hasX && action.hasWidth) {
        *target = QRect(action.x, action.y, action.width, action.height);
        if (!target->isValid()) {
            if (error) *error = QStringLiteral("The target geometry is invalid.");
            return false;
        }
        return true;
    }

    const QString placement = action.placement.trimmed().toLower();
    if (placement.isEmpty()) {
        if (error) *error = QStringLiteral("No placement or geometry was provided.");
        return false;
    }

    const QRect screen = m_windowManager ? m_windowManager->screenGeometry() : QRect();
    if (!screen.isValid() || screen.isEmpty()) {
        if (error) *error = QStringLiteral("Screen geometry is unavailable.");
        return false;
    }

    const int halfWidth = qMax(1, screen.width() / 2);
    const int halfHeight = qMax(1, screen.height() / 2);

    if (placement == QLatin1String("left")) {
        *target = QRect(screen.x(), screen.y(), halfWidth, screen.height());
    } else if (placement == QLatin1String("right")) {
        *target = QRect(screen.x() + halfWidth, screen.y(),
                        screen.width() - halfWidth, screen.height());
    } else if (placement == QLatin1String("top")) {
        *target = QRect(screen.x(), screen.y(), screen.width(), halfHeight);
    } else if (placement == QLatin1String("bottom")) {
        *target = QRect(screen.x(), screen.y() + halfHeight,
                        screen.width(), screen.height() - halfHeight);
    } else if (placement == QLatin1String("top-left")) {
        *target = QRect(screen.x(), screen.y(), halfWidth, halfHeight);
    } else if (placement == QLatin1String("top-right")) {
        *target = QRect(screen.x() + halfWidth, screen.y(),
                        screen.width() - halfWidth, halfHeight);
    } else if (placement == QLatin1String("bottom-left")) {
        *target = QRect(screen.x(), screen.y() + halfHeight,
                        halfWidth, screen.height() - halfHeight);
    } else if (placement == QLatin1String("bottom-right")) {
        *target = QRect(screen.x() + halfWidth, screen.y() + halfHeight,
                        screen.width() - halfWidth, screen.height() - halfHeight);
    } else if (placement == QLatin1String("maximized")) {
        *target = screen;
    } else if (placement == QLatin1String("center")) {
        QSize size = window.geometry.size();
        if (!size.isValid() || size.isEmpty()) {
            size = QSize(qMin(800, screen.width()), qMin(600, screen.height()));
        }
        *target = QRect(screen.x() + (screen.width() - size.width()) / 2,
                        screen.y() + (screen.height() - size.height()) / 2,
                        size.width(), size.height());
    } else {
        if (error) *error = QStringLiteral("Unsupported placement '%1'.").arg(placement);
        return false;
    }

    if (!target->isValid() || target->isEmpty()) {
        if (error) *error = QStringLiteral("The computed target geometry is invalid.");
        return false;
    }
    return true;
}

QString AutomationExecutor::formatWindow(const WindowInfo &window) const
{
    return QStringLiteral("%1 | %2 | %3 | %4x%5 at %6,%7")
        .arg(window.id,
             window.title.isEmpty() ? QStringLiteral("(untitled)") : window.title,
             window.appClass.isEmpty() ? window.appName : window.appClass)
        .arg(window.geometry.width())
        .arg(window.geometry.height())
        .arg(window.geometry.x())
        .arg(window.geometry.y());
}
