#ifndef TITANAI_COMMAND_SAFETY_VALIDATOR_HPP
#define TITANAI_COMMAND_SAFETY_VALIDATOR_HPP

#include <QString>
#include <QStringList>

#include "terminal/terminal_types.hpp"

struct TerminalValidationResult {
    TerminalRisk risk{TerminalRisk::Blocked};
    bool         allowed{false};
    bool         requiresSudo{false};
    QString      reason;
    QStringList  warnings;
};

/// Deterministic, model-independent safety layer for terminal commands.
///
/// This class is intentionally conservative. It uses an executable allowlist,
/// rejects shell metacharacters, privilege escalation and protected paths, and
/// never relies on the language model's own risk label.
class CommandSafetyValidator {
public:
    CommandSafetyValidator() = delete;

    static TerminalValidationResult validate(const TerminalCommandProposal &proposal);

    [[nodiscard]] static bool isAllowedExecutable(const QString &command);
    [[nodiscard]] static bool hasShellMetacharacters(const QString &text);
    [[nodiscard]] static bool isProtectedPath(const QString &path);
    [[nodiscard]] static QString resolveExecutable(const QString &command);
    [[nodiscard]] static QStringList lowRiskExecutables();
    [[nodiscard]] static QStringList mediumRiskExecutables();
    [[nodiscard]] static QStringList highRiskExecutables();

private:
    [[nodiscard]] static bool isWithinPermittedRoot(const QString &absolutePath);
    [[nodiscard]] static QString normalizePath(const QString &path,
                                               const QString &workingDirectory);
    [[nodiscard]] static bool commandDisallowsArguments(const QString &command,
                                                        const QStringList &arguments,
                                                        QString *reason);
    [[nodiscard]] static bool argumentsContainBlockedToken(const QStringList &arguments,
                                                           QString *reason);
};

#endif // TITANAI_COMMAND_SAFETY_VALIDATOR_HPP
