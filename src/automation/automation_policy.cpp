#include "automation/automation_policy.hpp"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include "terminal/command_safety_validator.hpp"
#include "terminal/terminal_types.hpp"

namespace {

bool hasControlCharacters(const QString &text)
{
    for (const QChar ch : text) {
        if (ch.category() == QChar::Other_Control) {
            return true;
        }
    }
    return false;
}

bool isLowRiskTerminalAction(const AutomationAction &action,
                             const AutomationPolicyContext &context,
                             QStringList *warnings,
                             QString *reason)
{
    if (!context.terminalCommandsEnabled) {
        *reason = QStringLiteral("Terminal commands are disabled for automation.");
        return false;
    }
    if (action.command.trimmed().isEmpty()) {
        *reason = QStringLiteral("Terminal command is empty.");
        return false;
    }
    if (action.arguments.size() > 32) {
        *reason = QStringLiteral("Terminal command has too many arguments.");
        return false;
    }
    for (const QString &argument : action.arguments) {
        if (argument.size() > 2048 || hasControlCharacters(argument)) {
            *reason = QStringLiteral("Terminal argument is not allowed.");
            return false;
        }
    }

    TerminalCommandProposal proposal;
    proposal.command = action.command.trimmed();
    proposal.arguments = action.arguments;
    proposal.explanation = action.description;
    proposal.modelRisk = TerminalRisk::Low;
    proposal.requiresSudo = false;
    proposal.workingDirectory = context.projectRoot.isEmpty()
                                    ? QDir::homePath()
                                    : context.projectRoot;

    const TerminalValidationResult result = CommandSafetyValidator::validate(proposal);
    if (!result.allowed) {
        *reason = QStringLiteral("Terminal command rejected: %1").arg(result.reason);
        return false;
    }
    if (result.requiresSudo || result.risk != TerminalRisk::Low) {
        *reason = QStringLiteral("Automation only allows read-only, low-risk terminal commands.");
        return false;
    }
    if (warnings) {
        warnings->append(result.warnings);
    }
    return true;
}

} // namespace

AutomationPolicyContext AutomationPolicy::contextFor(const QString &projectDirectory)
{
    AutomationPolicyContext context;
    context.projectRoot = projectDirectory.isEmpty()
                              ? QString()
                              : QDir(projectDirectory).absolutePath();

    QSet<QString> roots;
    const QString home = QDir::homePath();
    roots.insert(QDir(home).absolutePath());
    roots.insert(QDir::tempPath());
    if (!context.projectRoot.isEmpty() && QFileInfo(context.projectRoot).isDir()) {
        roots.insert(QFileInfo(context.projectRoot).canonicalFilePath());
        context.projectRoot = QFileInfo(context.projectRoot).canonicalFilePath();
    }

    for (const QString &root : roots) {
        if (!root.isEmpty() && root != QLatin1String("/")) {
            context.authorizedRoots.append(QDir::cleanPath(root));
        }
    }
    context.authorizedRoots.removeDuplicates();
    return context;
}

bool AutomationPolicy::isWithinRoot(const QString &path, const QString &root)
{
    if (path == root) {
        return true;
    }
    return path.startsWith(root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/'));
}

QString AutomationPolicy::resolvePath(const QString &path,
                                      const AutomationPolicyContext &context)
{
    QString expanded = path.trimmed();
    const QString home = QDir::homePath();
    if (expanded == QLatin1String("~")) {
        expanded = home;
    } else if (expanded.startsWith(QLatin1String("~/"))) {
        expanded = home + expanded.mid(1);
    }

    QFileInfo info(expanded);
    if (info.isRelative()) {
        const QString base = context.projectRoot.isEmpty() ? home : context.projectRoot;
        expanded = QDir(base).absoluteFilePath(expanded);
    }

    const QString cleaned = QDir::cleanPath(expanded);
    const QFileInfo resolved(cleaned);
    if (resolved.exists()) {
        const QString canonical = resolved.canonicalFilePath();
        if (!canonical.isEmpty()) {
            return QDir::cleanPath(canonical);
        }
    }
    return cleaned;
}

bool AutomationPolicy::isAuthorizedPath(const QString &path,
                                        const AutomationPolicyContext &context,
                                        QString *resolvedPath,
                                        QString *reason)
{
    const QString raw = path.trimmed();
    if (raw.isEmpty()) {
        if (reason) *reason = QStringLiteral("Path is empty.");
        return false;
    }
    if (raw.size() > 4096 || hasControlCharacters(raw)) {
        if (reason) *reason = QStringLiteral("Path is not allowed.");
        return false;
    }
    if (raw.startsWith(QLatin1Char('~')) && raw != QLatin1String("~")
        && !raw.startsWith(QLatin1String("~/"))) {
        if (reason) *reason = QStringLiteral("User-home expansion is not allowed.");
        return false;
    }

    const QString resolved = resolvePath(raw, context);
    if (resolved.isEmpty() || resolved == QLatin1String("/")) {
        if (reason) *reason = QStringLiteral("Path is not allowed.");
        return false;
    }
    if (CommandSafetyValidator::isProtectedPath(resolved)
        || CommandSafetyValidator::isProtectedPath(raw)) {
        if (reason) *reason = QStringLiteral("Path is protected.");
        return false;
    }

    for (const QString &root : context.authorizedRoots) {
        if (isWithinRoot(resolved, root)) {
            if (resolvedPath) *resolvedPath = resolved;
            return true;
        }
    }

    if (reason) {
        *reason = QStringLiteral("Path is outside the authorized directories.");
    }
    return false;
}

bool AutomationPolicy::isSafeApplicationName(const QString &name)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9 ._+@-]{0,79}$"));
    if (!pattern.match(name.trimmed()).hasMatch()) {
        return false;
    }
    static const QSet<QString> blocked{
        QStringLiteral("sh"), QStringLiteral("bash"), QStringLiteral("zsh"),
        QStringLiteral("fish"), QStringLiteral("dash"), QStringLiteral("ksh"),
        QStringLiteral("sudo"), QStringLiteral("su"), QStringLiteral("doas"),
        QStringLiteral("pkexec"), QStringLiteral("env"), QStringLiteral("python"),
        QStringLiteral("python3"), QStringLiteral("perl"), QStringLiteral("ruby"),
        QStringLiteral("node"), QStringLiteral("rm"), QStringLiteral("dd"),
    };
    return !blocked.contains(name.trimmed().toLower());
}

bool AutomationPolicy::isSafeWindowId(const QString &windowId)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._:xX-]{0,127}$"));
    return pattern.match(windowId.trimmed()).hasMatch();
}

AutomationPolicyResult AutomationPolicy::validatePlan(const AutomationPlan &plan,
                                                      const AutomationPolicyContext &context)
{
    AutomationPolicyResult result;
    const QString planId = plan.id.trimmed();
    if (planId.isEmpty() || planId.size() > 128) {
        result.reason = QStringLiteral("Plan id is missing or invalid.");
        return result;
    }
    if (plan.taskName.trimmed().isEmpty() || plan.taskName.size() > 200) {
        result.reason = QStringLiteral("Plan task name is missing or too long.");
        return result;
    }
    if (!plan.requiresApproval) {
        result.reason = QStringLiteral("Automation plans must require explicit approval.");
        return result;
    }
    if (plan.actions.isEmpty()) {
        result.reason = QStringLiteral("Plan has no actions.");
        return result;
    }
    if (plan.actions.size() > context.maxActions) {
        result.reason = QStringLiteral("Plan has %1 actions; the maximum is %2.")
                            .arg(plan.actions.size())
                            .arg(context.maxActions);
        return result;
    }
    if (!plan.fingerprint.isEmpty() && plan.fingerprint != plan.computeFingerprint()) {
        result.reason = QStringLiteral("Plan integrity check failed.");
        return result;
    }

    QSet<QString> ids;
    QHash<QString, int> signatures;
    for (const AutomationAction &action : plan.actions) {
        if (action.id.trimmed().isEmpty() || action.id.size() > 80
            || !QRegularExpression(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_-]*$"))
                    .match(action.id)
                    .hasMatch()) {
            result.reason = QStringLiteral("Action id is missing or invalid.");
            return result;
        }
        if (ids.contains(action.id)) {
            result.reason = QStringLiteral("Duplicate action id: %1").arg(action.id);
            return result;
        }
        ids.insert(action.id);

        const int count = ++signatures[action.signature()];
        if (count > context.maxIdenticalActions) {
            result.reason = QStringLiteral(
                "Plan repeats the same action more than %1 times.").arg(context.maxIdenticalActions);
            return result;
        }

        AutomationPolicyResult actionResult = validateAction(action, context);
        if (!actionResult.allowed) {
            result.reason = QStringLiteral("Action '%1': %2")
                                .arg(action.id, actionResult.reason);
            return result;
        }
        result.warnings.append(actionResult.warnings);
    }

    result.allowed = true;
    return result;
}

AutomationPolicyResult AutomationPolicy::validateAction(const AutomationAction &action,
                                                        const AutomationPolicyContext &context)
{
    AutomationPolicyResult result;

    if (action.description.size() > 1000) {
        result.reason = QStringLiteral("Action description is too long.");
        return result;
    }
    if (action.hasX != action.hasY || action.hasWidth != action.hasHeight) {
        result.reason = QStringLiteral("Incomplete geometry coordinates.");
        return result;
    }
    if (action.hasX && (action.x < -32768 || action.x > 32768
                        || action.y < -32768 || action.y > 32768)) {
        result.reason = QStringLiteral("Window position is out of range.");
        return result;
    }
    if (action.hasWidth && (action.width < 100 || action.width > 16384
                            || action.height < 100 || action.height > 16384)) {
        result.reason = QStringLiteral("Window size is out of range.");
        return result;
    }

    auto requireApplication = [&]() -> bool {
        if (action.application.trimmed().isEmpty()) {
            result.reason = QStringLiteral("An application name is required.");
            return false;
        }
        if (!isSafeApplicationName(action.application)) {
            result.reason = QStringLiteral("Application name is not allowed.");
            return false;
        }
        return true;
    };
    auto requireWindowTarget = [&]() -> bool {
        if (!action.windowId.trimmed().isEmpty()) {
            if (!isSafeWindowId(action.windowId)) {
                result.reason = QStringLiteral("Window id is not allowed.");
                return false;
            }
            return true;
        }
        return requireApplication();
    };

    switch (action.type) {
    case AutomationActionType::Unknown:
        result.reason = QStringLiteral("Unknown action type.");
        return result;

    case AutomationActionType::LaunchApplication:
        if (!requireApplication()) return result;
        break;

    case AutomationActionType::ListWindows:
        if (!action.application.isEmpty() && !isSafeApplicationName(action.application)) {
            result.reason = QStringLiteral("Application filter is not allowed.");
            return result;
        }
        break;

    case AutomationActionType::FocusWindow:
        if (!requireWindowTarget()) return result;
        break;

    case AutomationActionType::GetActiveWindow:
        break;

    case AutomationActionType::WaitForWindow:
        if (!requireWindowTarget()) return result;
        if (action.timeoutMs < 100 || action.timeoutMs > 30000) {
            result.reason = QStringLiteral("Window wait timeout must be 100-30000 ms.");
            return result;
        }
        break;

    case AutomationActionType::MoveWindow: {
        if (!requireWindowTarget()) return result;
        static const QSet<QString> placements{
            QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("top"),
            QStringLiteral("bottom"), QStringLiteral("center"),
            QStringLiteral("maximized"), QStringLiteral("top-left"),
            QStringLiteral("top-right"), QStringLiteral("bottom-left"),
            QStringLiteral("bottom-right"),
        };
        const QString placement = action.placement.trimmed().toLower();
        if (!placement.isEmpty() && !placements.contains(placement)) {
            result.reason = QStringLiteral("Window placement is not allowed.");
            return result;
        }
        if (placement.isEmpty() && (!action.hasWidth || !action.hasX)) {
            result.reason = QStringLiteral(
                "Move requires a placement or a full explicit geometry.");
            return result;
        }
        break;
    }

    case AutomationActionType::ResizeWindow:
        if (!requireWindowTarget()) return result;
        if (!action.hasWidth) {
            result.reason = QStringLiteral("Resize requires a width and height.");
            return result;
        }
        break;

    case AutomationActionType::OpenDirectory:
    case AutomationActionType::OpenFile: {
        QString resolved;
        QString pathReason;
        if (!isAuthorizedPath(action.path, context, &resolved, &pathReason)) {
            result.reason = pathReason;
            return result;
        }
        const QFileInfo info(resolved);
        if (!info.exists()) {
            result.reason = QStringLiteral("Path does not exist.");
            return result;
        }
        const bool wantsDirectory = action.type == AutomationActionType::OpenDirectory;
        if (wantsDirectory && !info.isDir()) {
            result.reason = QStringLiteral("Path is not a directory.");
            return result;
        }
        if (!wantsDirectory && !info.isFile()) {
            result.reason = QStringLiteral("Path is not a regular file.");
            return result;
        }
        result.resolvedPath = resolved;
        break;
    }

    case AutomationActionType::RunApprovedTerminalCommand: {
        QString terminalReason;
        if (!isLowRiskTerminalAction(action, context, &result.warnings, &terminalReason)) {
            result.reason = terminalReason;
            return result;
        }
        break;
    }
    }

    result.allowed = true;
    return result;
}
