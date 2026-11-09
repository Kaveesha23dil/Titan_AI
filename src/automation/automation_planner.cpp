#include "automation/automation_planner.hpp"

#include <QDir>
#include <algorithm>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

#include "automation/automation_policy.hpp"

namespace {

const QString kSystemPrompt = QStringLiteral(
    "You are TitanAI's desktop automation planner. Convert the user's request "
    "into a short, safe sequence of desktop actions. Reply with exactly ONE "
    "JSON object and nothing else. Never use Markdown, prose, shell operators, "
    "sudo, package managers, destructive commands or guessed window ids.");

QString stripCodeFences(const QString &text)
{
    QString cleaned = text.trimmed();
    if (!cleaned.startsWith(QStringLiteral("```"))) {
        return cleaned;
    }
    const int firstNewline = cleaned.indexOf(QLatin1Char('\n'));
    if (firstNewline >= 0) {
        cleaned = cleaned.mid(firstNewline + 1);
    }
    const int closing = cleaned.lastIndexOf(QStringLiteral("```"));
    if (closing >= 0) {
        cleaned = cleaned.left(closing);
    }
    return cleaned.trimmed();
}

QString extractJsonObject(const QString &text)
{
    const QString cleaned = stripCodeFences(text);
    const int start = cleaned.indexOf(QLatin1Char('{'));
    const int end = cleaned.lastIndexOf(QLatin1Char('}'));
    if (start < 0 || end <= start) {
        return {};
    }
    return cleaned.mid(start, end - start + 1);
}

QStringList stringListFromValue(const QJsonValue &value)
{
    QStringList result;
    for (const QJsonValue &entry : value.toArray()) {
        if (entry.isString()) {
            const QString text = entry.toString().trimmed();
            if (!text.isEmpty()) {
                result.append(text);
            }
        }
    }
    return result;
}

QString sanitizedActionId(const QString &candidate, int index)
{
    QString id = candidate.trimmed();
    id.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")),
               QStringLiteral("_"));
    if (id.isEmpty() || !id.at(0).isLetterOrNumber()) {
        id = QStringLiteral("action_%1").arg(index + 1);
    }
    if (id.size() > 80) {
        id.truncate(80);
    }
    return id;
}

QString environmentSummary(const DesktopEnvironmentInfo &environment)
{
    return QStringLiteral(
               "Session: %1; desktop: %2; compositor: %3; window backend: %4; "
               "window management supported: %5")
        .arg(environment.sessionType.isEmpty() ? QStringLiteral("unknown")
                                               : environment.sessionType,
             environment.desktopName.isEmpty() ? QStringLiteral("unknown")
                                               : environment.desktopName,
             environment.compositor.isEmpty() ? QStringLiteral("unknown")
                                              : environment.compositor,
             environment.windowBackend.isEmpty() ? QStringLiteral("none")
                                                 : environment.windowBackend,
             environment.windowManagementSupported ? QStringLiteral("yes")
                                                   : QStringLiteral("no"));
}

} // namespace

AutomationPlanner::AutomationPlanner(QObject *parent)
    : QObject(parent)
    , m_client(this)
    , m_environment(DesktopEnvironmentDetector::detect())
{
    connect(&m_client, &OllamaClient::completionReceived,
            this, &AutomationPlanner::onCompletionReceived);
    connect(&m_client, &OllamaClient::completionError,
            this, &AutomationPlanner::onCompletionError);
}

void AutomationPlanner::setModel(const QString &model)
{
    m_client.setModel(model);
}

QString AutomationPlanner::model() const
{
    return m_client.model();
}

bool AutomationPlanner::isBusy() const
{
    return m_busy;
}

void AutomationPlanner::setEnvironment(const DesktopEnvironmentInfo &environment)
{
    m_environment = environment;
}

void AutomationPlanner::setProjectDirectory(const QString &directory)
{
    m_projectDirectory = directory;
}

bool AutomationPlanner::isAutomationRequest(const QString &message)
{
    const QString lower = message.toLower().simplified();
    if (lower.isEmpty()) {
        return false;
    }

    const bool looksInformational =
        lower.contains(QStringLiteral("how to"))
        || lower.contains(QStringLiteral("how do i"))
        || lower.contains(QStringLiteral("how can i"))
        || lower.contains(QStringLiteral("tutorial"))
        || lower.contains(QStringLiteral("guide"))
        || lower.contains(QStringLiteral("explain"))
        || lower.contains(QStringLiteral("what does"))
        || lower.contains(QStringLiteral("why does"))
        || lower.contains(QStringLiteral("open source"));
    if (looksInformational) {
        return false;
    }

    static const QRegularExpression openVerb(
        QStringLiteral("\\b(open|launch)\\b"));
    static const QRegularExpression startApp(
        QStringLiteral("\\bstart\\b.*\\b(app|application|program|window)\\b"));
    static const QRegularExpression moveWindow(
        QStringLiteral("\\bmove\\b.*\\b(window|app|application|terminal|browser|editor|"
                       "code|firefox|file manager)\\b"));
    static const QRegularExpression resizeWindow(
        QStringLiteral("\\b(resize|maximize|focus|tile|arrange)\\b.*\\b(window|app|"
                       "application|terminal|browser|editor|code|firefox|panel)\\b"));
    static const QRegularExpression activeWindow(
        QStringLiteral("\\b(active|current|focused)\\s+window\\b"));
    static const QRegularExpression listWindows(
        QStringLiteral("\\b(list|show|what|which)\\b.*\\bwindows?\\b"));

    static const QStringList strongPhrases = {
        QStringLiteral("side by side"),
        QStringLiteral("split screen"),
        QStringLiteral("split the screen"),
        QStringLiteral("desktop automation"),
        QStringLiteral("automate my desktop"),
        QStringLiteral("automate the desktop"),
        QStringLiteral("window management"),
        QStringLiteral("open file"),
        QStringLiteral("open folder"),
        QStringLiteral("open directory"),
        QStringLiteral("open my project"),
        QStringLiteral("open the project"),
        QStringLiteral("launch the app"),
        QStringLiteral("open the app"),
        QStringLiteral("bring to front"),
        QStringLiteral("bring it to the front"),
        QStringLiteral("switch to the window"),
        QStringLiteral("list windows"),
        QStringLiteral("what windows"),
        QStringLiteral("windows are open"),
    };

    for (const QString &phrase : strongPhrases) {
        if (lower.contains(phrase)) {
            return true;
        }
    }

    return openVerb.match(lower).hasMatch()
           || startApp.match(lower).hasMatch()
           || moveWindow.match(lower).hasMatch()
           || resizeWindow.match(lower).hasMatch()
           || activeWindow.match(lower).hasMatch()
           || listWindows.match(lower).hasMatch();
}

QString AutomationPlanner::buildPrompt(const QString &request,
                                       const DesktopEnvironmentInfo &environment,
                                       const QString &projectDirectory,
                                       const QStringList &installedApplications)
{
    QString appHints = installedApplications.mid(0, 100).join(QStringLiteral(", "));
    if (appHints.isEmpty()) {
        appHints = QStringLiteral("(none detected)");
    }

    return QStringLiteral(
               "%1\n\n"
               "Environment: %2\n"
               "Project directory: %3\n"
               "Installed applications: %4\n\n"
               "Return one JSON object with this schema:\n"
               "{\n"
               "  \"task_name\": \"short title\",\n"
               "  \"warnings\": [\"optional\"],\n"
               "  \"actions\": [\n"
               "    {\n"
               "      \"id\": \"action_1\",\n"
               "      \"type\": \"launch_application\",\n"
               "      \"application\": \"application name\",\n"
               "      \"window_id\": \"\",\n"
               "      \"path\": \"\",\n"
               "      \"placement\": \"\",\n"
               "      \"x\": 0, \"y\": 0, \"width\": 0, \"height\": 0,\n"
               "      \"command\": \"\",\n"
               "      \"arguments\": [],\n"
               "      \"description\": \"what this step does\",\n"
               "      \"required\": true,\n"
               "      \"timeout_ms\": 10000\n"
               "    }\n"
               "  ]\n"
               "}\n\n"
               "Allowed action types: launch_application, focus_window, list_windows, "
               "move_window, resize_window, open_directory, open_file, "
               "get_active_window, wait_for_window, run_approved_terminal_command.\n"
               "Rules:\n"
               "- Use explicit application names from the installed list; never invent "
               "window ids and never include shell commands in application names.\n"
               "- Use placement instead of coordinates when possible: left, right, top, "
               "bottom, center, maximized, top-left, top-right, bottom-left, bottom-right.\n"
               "- For explicit geometry provide all of x, y, width and height together.\n"
               "- Only path actions to files/directories inside the project, the home "
               "directory or /tmp. Paths may be relative to the project directory.\n"
               "- run_approved_terminal_command is only for low-risk read-only commands "
               "such as git status, git log, ls or pwd. Never use sudo, shell operators, "
               "redirection, pipes or destructive commands.\n"
               "- Keep plans under 12 actions. Prefer one action per requested app.\n"
               "- If a window backend is not supported, still return the intended actions "
               "and add a warning.\n\n"
               "User request: %5")
        .arg(kSystemPrompt, environmentSummary(environment),
             projectDirectory.isEmpty() ? QStringLiteral("(not selected)")
                                        : projectDirectory,
             appHints, request);
}

bool AutomationPlanner::parsePlan(const QString &response, const QString &request,
                                  AutomationPlan *plan, QString *error)
{
    if (!plan) {
        if (error) *error = QStringLiteral("No plan output provided.");
        return false;
    }

    const QString json = extractJsonObject(response);
    if (json.isEmpty()) {
        if (error) *error = QStringLiteral("The model did not return a JSON plan.");
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("Invalid JSON plan: %1").arg(parseError.errorString());
        }
        return false;
    }

    const QJsonObject obj = document.object();
    const QJsonValue actionsValue = obj.value(QStringLiteral("actions"));
    if (!actionsValue.isArray() || actionsValue.toArray().isEmpty()) {
        if (error) *error = QStringLiteral("The plan contains no actions.");
        return false;
    }

    AutomationPlan parsed;
    parsed.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    parsed.taskName = obj.value(QStringLiteral("task_name")).toString().trimmed();
    if (parsed.taskName.isEmpty()) {
        parsed.taskName = QStringLiteral("Desktop task");
    }
    if (parsed.taskName.size() > 200) {
        parsed.taskName.truncate(200);
    }
    parsed.request = request.trimmed();
    parsed.warnings = stringListFromValue(obj.value(QStringLiteral("warnings")));
    parsed.requiresApproval = true;
    parsed.createdAt = QDateTime::currentDateTime();

    QSet<QString> usedIds;
    int index = 0;
    for (const QJsonValue &value : actionsValue.toArray()) {
        if (!value.isObject()) {
            if (error) *error = QStringLiteral("Each action must be a JSON object.");
            return false;
        }
        const QJsonObject actionObj = value.toObject();
        const QString typeName = actionObj.value(QStringLiteral("type")).toString();
        if (automationActionTypeFromName(typeName) == AutomationActionType::Unknown) {
            if (error) {
                *error = QStringLiteral("Unsupported action type: %1").arg(typeName);
            }
            return false;
        }

        AutomationAction action = AutomationAction::fromJson(actionObj);
        QString id = sanitizedActionId(action.id, index);
        QString unique = id;
        int suffix = 2;
        while (usedIds.contains(unique)) {
            unique = QStringLiteral("%1_%2").arg(id).arg(suffix++);
        }
        usedIds.insert(unique);
        action.id = unique;
        action.required = actionObj.value(QStringLiteral("required")).toBool(true);
        if (action.description.isEmpty()) {
            action.description = automationActionTypeName(action.type);
        }
        parsed.actions.append(action);
        ++index;
    }

    *plan = parsed;
    return true;
}

void AutomationPlanner::generatePlan(const QString &request)
{
    if (m_busy) {
        return;
    }
    const QString trimmed = request.trimmed();
    if (trimmed.isEmpty()) {
        emit planningFailed(QStringLiteral("The desktop request is empty."));
        return;
    }

    QStringList applications;
    for (const ApplicationInfo &app : m_launcher.applications()) {
        applications.append(app.name.isEmpty() ? app.id : app.name);
    }
    applications.removeDuplicates();
    applications.sort(Qt::CaseInsensitive);

    m_busy = true;
    m_pendingRequest = trimmed;
    m_environment = DesktopEnvironmentDetector::detect();
    emit planningStarted(trimmed);
    m_client.requestCompletion(
        buildPrompt(trimmed, m_environment, m_projectDirectory, applications));
}

void AutomationPlanner::onCompletionReceived(const QString &response)
{
    if (!m_busy) {
        return;
    }

    AutomationPlan plan;
    QString parseError;
    if (!parsePlan(response, m_pendingRequest, &plan, &parseError)) {
        finishWithError(parseError);
        return;
    }

    enrichPlan(&plan);
    const AutomationPolicyContext context =
        AutomationPolicy::contextFor(m_projectDirectory);
    const AutomationPolicyResult policy = AutomationPolicy::validatePlan(plan, context);
    if (!policy.allowed) {
        finishWithError(QStringLiteral("Plan rejected by safety policy: %1")
                            .arg(policy.reason));
        return;
    }
    if (!policy.warnings.isEmpty()) {
        plan.warnings.append(policy.warnings);
        plan.warnings.removeDuplicates();
    }

    plan.fingerprint = plan.computeFingerprint();
    m_busy = false;
    m_pendingRequest.clear();
    emit planReady(plan);
}

void AutomationPlanner::onCompletionError(const QString &error)
{
    if (!m_busy) {
        return;
    }
    finishWithError(error);
}

void AutomationPlanner::finishWithError(const QString &error)
{
    m_busy = false;
    m_pendingRequest.clear();
    emit planningFailed(error);
}

void AutomationPlanner::enrichPlan(AutomationPlan *plan)
{
    if (!plan) {
        return;
    }

    auto markUnsupported = [&](AutomationAction &action, const QString &reason) {
        action.supported = false;
        action.unsupportedReason = reason;
        const QString warning = QStringLiteral("%1 (%2)").arg(reason, action.id);
        action.warnings.append(warning);
        plan->warnings.append(warning);
    };

    const bool windowsSupported = m_environment.windowManagementSupported;
    const QString windowReason = m_environment.unsupportedReason.isEmpty()
        ? QStringLiteral("Window management is not supported on this desktop.")
        : m_environment.unsupportedReason;

    for (AutomationAction &action : plan->actions) {
        switch (action.type) {
        case AutomationActionType::LaunchApplication:
            if (!action.application.isEmpty()
                && !m_launcher.isInstalled(action.application)) {
                markUnsupported(
                    action,
                    QStringLiteral("Application '%1' is not installed.")
                        .arg(action.application));
            }
            break;

        case AutomationActionType::FocusWindow:
        case AutomationActionType::WaitForWindow:
            if (!windowsSupported) {
                markUnsupported(action, windowReason);
            } else if (action.windowId.isEmpty() && !action.application.isEmpty()
                       && !m_launcher.isInstalled(action.application)) {
                markUnsupported(
                    action,
                    QStringLiteral("Application '%1' is not installed.")
                        .arg(action.application));
            }
            break;

        case AutomationActionType::ListWindows:
        case AutomationActionType::GetActiveWindow:
        case AutomationActionType::MoveWindow:
        case AutomationActionType::ResizeWindow:
            if (!windowsSupported) {
                markUnsupported(action, windowReason);
            } else if ((action.type == AutomationActionType::MoveWindow
                        || action.type == AutomationActionType::ResizeWindow)
                       && action.windowId.isEmpty() && !action.application.isEmpty()
                       && !m_launcher.isInstalled(action.application)) {
                markUnsupported(
                    action,
                    QStringLiteral("Application '%1' is not installed.")
                        .arg(action.application));
            }
            break;

        case AutomationActionType::OpenDirectory:
        case AutomationActionType::OpenFile:
        case AutomationActionType::RunApprovedTerminalCommand:
        case AutomationActionType::Unknown:
            break;
        }
    }

    plan->supported = std::all_of(plan->actions.cbegin(), plan->actions.cend(),
                                  [](const AutomationAction &action) {
                                      return action.supported;
                                  });
    plan->warnings.removeDuplicates();
}
