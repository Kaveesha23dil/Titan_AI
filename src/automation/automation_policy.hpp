#ifndef TITANAI_AUTOMATION_POLICY_HPP
#define TITANAI_AUTOMATION_POLICY_HPP

#include <QString>
#include <QStringList>

#include "automation/automation_types.hpp"

struct AutomationPolicyContext {
    QStringList authorizedRoots;
    QString     projectRoot;
    int         maxActions{12};
    int         maxIdenticalActions{2};
    bool        terminalCommandsEnabled{true};
};

struct AutomationPolicyResult {
    bool        allowed{false};
    QString     reason;
    QStringList warnings;
    QString     resolvedPath;
};

class AutomationPolicy {
public:
    AutomationPolicy() = delete;

    [[nodiscard]] static AutomationPolicyContext contextFor(const QString &projectDirectory);

    [[nodiscard]] static AutomationPolicyResult validatePlan(
        const AutomationPlan &plan,
        const AutomationPolicyContext &context);

    [[nodiscard]] static AutomationPolicyResult validateAction(
        const AutomationAction &action,
        const AutomationPolicyContext &context);

    [[nodiscard]] static bool isAuthorizedPath(const QString &path,
                                               const AutomationPolicyContext &context,
                                               QString *resolvedPath = nullptr,
                                               QString *reason = nullptr);

    [[nodiscard]] static bool isSafeApplicationName(const QString &name);
    [[nodiscard]] static bool isSafeWindowId(const QString &windowId);

private:
    [[nodiscard]] static bool isWithinRoot(const QString &path, const QString &root);
    [[nodiscard]] static QString resolvePath(const QString &path,
                                             const AutomationPolicyContext &context);
};

#endif // TITANAI_AUTOMATION_POLICY_HPP
