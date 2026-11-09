#include "automation/automation_types.hpp"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QUuid>

QString automationActionTypeName(AutomationActionType type)
{
    switch (type) {
    case AutomationActionType::LaunchApplication:          return QStringLiteral("launch_application");
    case AutomationActionType::FocusWindow:                return QStringLiteral("focus_window");
    case AutomationActionType::ListWindows:                return QStringLiteral("list_windows");
    case AutomationActionType::MoveWindow:                 return QStringLiteral("move_window");
    case AutomationActionType::ResizeWindow:               return QStringLiteral("resize_window");
    case AutomationActionType::OpenDirectory:              return QStringLiteral("open_directory");
    case AutomationActionType::OpenFile:                   return QStringLiteral("open_file");
    case AutomationActionType::GetActiveWindow:            return QStringLiteral("get_active_window");
    case AutomationActionType::WaitForWindow:              return QStringLiteral("wait_for_window");
    case AutomationActionType::RunApprovedTerminalCommand: return QStringLiteral("run_approved_terminal_command");
    case AutomationActionType::Unknown:                    return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

AutomationActionType automationActionTypeFromName(const QString &name)
{
    const QString lower = name.trimmed().toLower();
    if (lower == QLatin1String("launch_application"))          return AutomationActionType::LaunchApplication;
    if (lower == QLatin1String("focus_window"))                return AutomationActionType::FocusWindow;
    if (lower == QLatin1String("list_windows"))                return AutomationActionType::ListWindows;
    if (lower == QLatin1String("move_window"))                 return AutomationActionType::MoveWindow;
    if (lower == QLatin1String("resize_window"))               return AutomationActionType::ResizeWindow;
    if (lower == QLatin1String("open_directory"))              return AutomationActionType::OpenDirectory;
    if (lower == QLatin1String("open_file"))                   return AutomationActionType::OpenFile;
    if (lower == QLatin1String("get_active_window"))           return AutomationActionType::GetActiveWindow;
    if (lower == QLatin1String("wait_for_window"))             return AutomationActionType::WaitForWindow;
    if (lower == QLatin1String("run_approved_terminal_command")) return AutomationActionType::RunApprovedTerminalCommand;
    return AutomationActionType::Unknown;
}

QString automationActionStatusName(AutomationActionStatus status)
{
    switch (status) {
    case AutomationActionStatus::Pending:            return QStringLiteral("pending");
    case AutomationActionStatus::WaitingForApproval: return QStringLiteral("waiting");
    case AutomationActionStatus::Running:            return QStringLiteral("running");
    case AutomationActionStatus::Completed:          return QStringLiteral("completed");
    case AutomationActionStatus::Failed:             return QStringLiteral("failed");
    case AutomationActionStatus::Cancelled:          return QStringLiteral("cancelled");
    case AutomationActionStatus::Unsupported:        return QStringLiteral("unsupported");
    case AutomationActionStatus::Skipped:            return QStringLiteral("skipped");
    }
    return QStringLiteral("pending");
}

AutomationActionStatus automationActionStatusFromName(const QString &name)
{
    const QString lower = name.trimmed().toLower();
    if (lower == QLatin1String("pending"))   return AutomationActionStatus::Pending;
    if (lower == QLatin1String("waiting"))   return AutomationActionStatus::WaitingForApproval;
    if (lower == QLatin1String("running"))   return AutomationActionStatus::Running;
    if (lower == QLatin1String("completed")) return AutomationActionStatus::Completed;
    if (lower == QLatin1String("failed"))    return AutomationActionStatus::Failed;
    if (lower == QLatin1String("cancelled")) return AutomationActionStatus::Cancelled;
    if (lower == QLatin1String("unsupported")) return AutomationActionStatus::Unsupported;
    if (lower == QLatin1String("skipped"))   return AutomationActionStatus::Skipped;
    return AutomationActionStatus::Pending;
}

QString automationWorkflowStatusName(AutomationWorkflowStatus status)
{
    switch (status) {
    case AutomationWorkflowStatus::Pending:            return QStringLiteral("pending");
    case AutomationWorkflowStatus::WaitingForApproval: return QStringLiteral("waiting");
    case AutomationWorkflowStatus::Running:            return QStringLiteral("running");
    case AutomationWorkflowStatus::Completed:          return QStringLiteral("completed");
    case AutomationWorkflowStatus::Failed:             return QStringLiteral("failed");
    case AutomationWorkflowStatus::Cancelled:          return QStringLiteral("cancelled");
    case AutomationWorkflowStatus::Unsupported:        return QStringLiteral("unsupported");
    }
    return QStringLiteral("pending");
}

AutomationWorkflowStatus automationWorkflowStatusFromName(const QString &name)
{
    const QString lower = name.trimmed().toLower();
    if (lower == QLatin1String("pending"))   return AutomationWorkflowStatus::Pending;
    if (lower == QLatin1String("waiting"))   return AutomationWorkflowStatus::WaitingForApproval;
    if (lower == QLatin1String("running"))   return AutomationWorkflowStatus::Running;
    if (lower == QLatin1String("completed")) return AutomationWorkflowStatus::Completed;
    if (lower == QLatin1String("failed"))    return AutomationWorkflowStatus::Failed;
    if (lower == QLatin1String("cancelled")) return AutomationWorkflowStatus::Cancelled;
    if (lower == QLatin1String("unsupported")) return AutomationWorkflowStatus::Unsupported;
    return AutomationWorkflowStatus::Pending;
}

namespace {

QJsonArray stringListToJson(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values) {
        array.append(value);
    }
    return array;
}

QStringList stringListFromJson(const QJsonValue &value)
{
    QStringList result;
    for (const QJsonValue &entry : value.toArray()) {
        if (entry.isString()) {
            result.append(entry.toString());
        }
    }
    return result;
}

int optionalInt(const QJsonObject &obj, const QString &key, int fallback, bool *present)
{
    if (!obj.contains(key) || !obj.value(key).isDouble()) {
        if (present) *present = false;
        return fallback;
    }
    if (present) *present = true;
    return obj.value(key).toInt(fallback);
}

} // namespace

QJsonObject AutomationAction::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("id")]          = id;
    obj[QStringLiteral("type")]        = automationActionTypeName(type);
    obj[QStringLiteral("application")] = application;
    obj[QStringLiteral("window_id")]   = windowId;
    obj[QStringLiteral("path")]        = path;
    obj[QStringLiteral("placement")]   = placement;
    obj[QStringLiteral("command")]     = command;
    obj[QStringLiteral("arguments")]   = stringListToJson(arguments);
    obj[QStringLiteral("description")] = description;
    obj[QStringLiteral("required")]    = required;
    obj[QStringLiteral("supported")]   = supported;
    obj[QStringLiteral("focus_existing_if_open")] = focusExistingIfOpen;
    obj[QStringLiteral("timeout_ms")]  = timeoutMs;
    obj[QStringLiteral("unsupported_reason")] = unsupportedReason;
    obj[QStringLiteral("warnings")]    = stringListToJson(warnings);

    if (hasX) obj[QStringLiteral("x")] = x;
    if (hasY) obj[QStringLiteral("y")] = y;
    if (hasWidth) obj[QStringLiteral("width")] = width;
    if (hasHeight) obj[QStringLiteral("height")] = height;

    return obj;
}

AutomationAction AutomationAction::fromJson(const QJsonObject &obj)
{
    AutomationAction action;
    action.id          = obj.value(QStringLiteral("id")).toString().trimmed();
    action.type        = automationActionTypeFromName(obj.value(QStringLiteral("type")).toString());
    action.application = obj.value(QStringLiteral("application")).toString().trimmed();
    action.windowId    = obj.value(QStringLiteral("window_id")).toString().trimmed();
    action.path        = obj.value(QStringLiteral("path")).toString().trimmed();
    action.placement   = obj.value(QStringLiteral("placement")).toString().trimmed().toLower();
    action.command     = obj.value(QStringLiteral("command")).toString().trimmed();
    action.arguments   = stringListFromJson(obj.value(QStringLiteral("arguments")));
    action.description = obj.value(QStringLiteral("description")).toString().trimmed();
    action.required    = obj.value(QStringLiteral("required")).toBool(true);
    action.supported   = obj.value(QStringLiteral("supported")).toBool(true);
    action.focusExistingIfOpen =
        obj.value(QStringLiteral("focus_existing_if_open")).toBool(true);
    action.timeoutMs   = obj.value(QStringLiteral("timeout_ms")).toInt(10000);
    action.unsupportedReason =
        obj.value(QStringLiteral("unsupported_reason")).toString().trimmed();
    action.warnings = stringListFromJson(obj.value(QStringLiteral("warnings")));

    action.x = optionalInt(obj, QStringLiteral("x"), -1, &action.hasX);
    action.y = optionalInt(obj, QStringLiteral("y"), -1, &action.hasY);
    action.width = optionalInt(obj, QStringLiteral("width"), -1, &action.hasWidth);
    action.height = optionalInt(obj, QStringLiteral("height"), -1, &action.hasHeight);

    return action;
}

QString AutomationAction::signature() const
{
    QJsonObject obj;
    obj[QStringLiteral("type")]        = automationActionTypeName(type);
    obj[QStringLiteral("application")] = application.toLower();
    obj[QStringLiteral("window_id")]   = windowId;
    obj[QStringLiteral("path")]        = path;
    obj[QStringLiteral("placement")]   = placement;
    obj[QStringLiteral("command")]     = command;
    obj[QStringLiteral("arguments")]   = stringListToJson(arguments);
    if (hasX) obj[QStringLiteral("x")] = x;
    if (hasY) obj[QStringLiteral("y")] = y;
    if (hasWidth) obj[QStringLiteral("width")] = width;
    if (hasHeight) obj[QStringLiteral("height")] = height;
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QJsonObject AutomationPlan::toJson() const
{
    QJsonArray actionArray;
    for (const AutomationAction &action : actions) {
        actionArray.append(action.toJson());
    }

    QJsonObject obj;
    obj[QStringLiteral("id")]                = id;
    obj[QStringLiteral("task_name")]         = taskName;
    obj[QStringLiteral("request")]           = request;
    obj[QStringLiteral("warnings")]          = stringListToJson(warnings);
    obj[QStringLiteral("actions")]           = actionArray;
    obj[QStringLiteral("requires_approval")] = requiresApproval;
    obj[QStringLiteral("supported")]         = supported;
    obj[QStringLiteral("fingerprint")]       = fingerprint;
    obj[QStringLiteral("created_at")]        = createdAt.toString(Qt::ISODate);
    return obj;
}

AutomationPlan AutomationPlan::fromJson(const QJsonObject &obj)
{
    AutomationPlan plan;
    plan.id               = obj.value(QStringLiteral("id")).toString().trimmed();
    plan.taskName         = obj.value(QStringLiteral("task_name")).toString().trimmed();
    plan.request          = obj.value(QStringLiteral("request")).toString().trimmed();
    plan.warnings         = stringListFromJson(obj.value(QStringLiteral("warnings")));
    plan.requiresApproval = obj.value(QStringLiteral("requires_approval")).toBool(true);
    plan.supported        = obj.value(QStringLiteral("supported")).toBool(true);
    plan.fingerprint       = obj.value(QStringLiteral("fingerprint")).toString().trimmed();
    plan.createdAt        = QDateTime::fromString(
        obj.value(QStringLiteral("created_at")).toString(), Qt::ISODate);

    for (const QJsonValue &value : obj.value(QStringLiteral("actions")).toArray()) {
        if (value.isObject()) {
            plan.actions.append(AutomationAction::fromJson(value.toObject()));
        }
    }
    return plan;
}

QString AutomationPlan::computeFingerprint() const
{
    QJsonArray actionArray;
    for (const AutomationAction &action : actions) {
        actionArray.append(action.toJson());
    }

    QJsonObject obj;
    obj[QStringLiteral("id")]                = id;
    obj[QStringLiteral("task_name")]         = taskName;
    obj[QStringLiteral("requires_approval")] = requiresApproval;
    obj[QStringLiteral("actions")]           = actionArray;

    const QByteArray canonical = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(
        QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex());
}

QJsonObject AutomationActionResult::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("action_id")]   = actionId;
    obj[QStringLiteral("description")] = description;
    obj[QStringLiteral("status")]      = automationActionStatusName(status);
    obj[QStringLiteral("message")]     = message;
    obj[QStringLiteral("output")]      = output;
    obj[QStringLiteral("duration_ms")] = static_cast<double>(durationMs);
    return obj;
}

AutomationActionResult AutomationActionResult::fromJson(const QJsonObject &obj)
{
    AutomationActionResult result;
    result.actionId    = obj.value(QStringLiteral("action_id")).toString().trimmed();
    result.description = obj.value(QStringLiteral("description")).toString();
    result.status = automationActionStatusFromName(
        obj.value(QStringLiteral("status")).toString());
    result.message     = obj.value(QStringLiteral("message")).toString();
    result.output      = obj.value(QStringLiteral("output")).toString();
    result.durationMs  = static_cast<qint64>(
        obj.value(QStringLiteral("duration_ms")).toDouble(0));
    return result;
}

QJsonObject AutomationHistoryEntry::toJson() const
{
    QJsonArray resultArray;
    for (const AutomationActionResult &result : results) {
        resultArray.append(result.toJson());
    }

    QJsonObject obj;
    obj[QStringLiteral("workflow_id")] = workflowId;
    obj[QStringLiteral("request")]     = request;
    obj[QStringLiteral("task_name")]   = taskName;
    obj[QStringLiteral("plan")]        = plan.toJson();
    obj[QStringLiteral("status")]      = automationWorkflowStatusName(status);
    obj[QStringLiteral("started_at")]  = startedAt.toString(Qt::ISODate);
    obj[QStringLiteral("finished_at")] = finishedAt.toString(Qt::ISODate);
    obj[QStringLiteral("results")]     = resultArray;
    obj[QStringLiteral("error")]       = error;
    return obj;
}

AutomationHistoryEntry AutomationHistoryEntry::fromJson(const QJsonObject &obj)
{
    AutomationHistoryEntry entry;
    entry.workflowId = obj.value(QStringLiteral("workflow_id")).toString().trimmed();
    entry.request    = obj.value(QStringLiteral("request")).toString();
    entry.taskName   = obj.value(QStringLiteral("task_name")).toString();
    entry.plan       = AutomationPlan::fromJson(obj.value(QStringLiteral("plan")).toObject());
    entry.status     = automationWorkflowStatusFromName(
        obj.value(QStringLiteral("status")).toString());
    entry.startedAt  = QDateTime::fromString(
        obj.value(QStringLiteral("started_at")).toString(), Qt::ISODate);
    entry.finishedAt = QDateTime::fromString(
        obj.value(QStringLiteral("finished_at")).toString(), Qt::ISODate);
    entry.error      = obj.value(QStringLiteral("error")).toString();

    for (const QJsonValue &value : obj.value(QStringLiteral("results")).toArray()) {
        if (value.isObject()) {
            entry.results.append(AutomationActionResult::fromJson(value.toObject()));
        }
    }
    return entry;
}
