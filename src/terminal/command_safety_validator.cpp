#include "terminal/command_safety_validator.hpp"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {

const QStringList kLowRisk = {
    QStringLiteral("free"),     QStringLiteral("df"),
    QStringLiteral("du"),       QStringLiteral("ls"),
    QStringLiteral("ps"),       QStringLiteral("pgrep"),
    QStringLiteral("uptime"),   QStringLiteral("uname"),
    QStringLiteral("whoami"),   QStringLiteral("id"),
    QStringLiteral("date"),     QStringLiteral("hostname"),
    QStringLiteral("lscpu"),    QStringLiteral("lsblk"),
    QStringLiteral("lspci"),    QStringLiteral("lsusb"),
    QStringLiteral("find"),     QStringLiteral("stat"),
    QStringLiteral("wc"),       QStringLiteral("pwd"),
    QStringLiteral("echo"),     QStringLiteral("which"),
    QStringLiteral("whereis"),  QStringLiteral("sleep"),
    QStringLiteral("git")
};

const QStringList kMediumRisk = {
    QStringLiteral("mkdir"), QStringLiteral("touch"),
    QStringLiteral("cp"),    QStringLiteral("mv"),
    QStringLiteral("ln")
};

// Every executable here is blocked from execution in the initial release.
const QStringList kHighRisk = {
    QStringLiteral("rm"),         QStringLiteral("rmdir"),
    QStringLiteral("unlink"),     QStringLiteral("shred"),
    QStringLiteral("chmod"),      QStringLiteral("chown"),
    QStringLiteral("chgrp"),      QStringLiteral("chattr"),
    QStringLiteral("setcap"),     QStringLiteral("dd"),
    QStringLiteral("truncate"),   QStringLiteral("fallocate"),
    QStringLiteral("mkfs"),       QStringLiteral("mkswap"),
    QStringLiteral("mount"),      QStringLiteral("umount"),
    QStringLiteral("swapon"),     QStringLiteral("swapoff"),
    QStringLiteral("fdisk"),      QStringLiteral("parted"),
    QStringLiteral("kill"),       QStringLiteral("pkill"),
    QStringLiteral("killall"),    QStringLiteral("systemctl"),
    QStringLiteral("service"),    QStringLiteral("shutdown"),
    QStringLiteral("reboot"),     QStringLiteral("poweroff"),
    QStringLiteral("halt"),       QStringLiteral("init"),
    QStringLiteral("pacman"),     QStringLiteral("yay"),
    QStringLiteral("paru"),       QStringLiteral("apt"),
    QStringLiteral("apt-get"),    QStringLiteral("dnf"),
    QStringLiteral("yum"),        QStringLiteral("zypper"),
    QStringLiteral("sudo"),       QStringLiteral("su"),
    QStringLiteral("pkexec"),     QStringLiteral("doas"),
    QStringLiteral("chroot"),     QStringLiteral("passwd"),
    QStringLiteral("useradd"),    QStringLiteral("usermod"),
    QStringLiteral("userdel"),    QStringLiteral("groupadd"),
    QStringLiteral("crontab"),    QStringLiteral("visudo"),
    QStringLiteral("iptables"),   QStringLiteral("nft"),
    QStringLiteral("ufw"),        QStringLiteral("firewall-cmd"),
    QStringLiteral("modprobe"),   QStringLiteral("insmod"),
    QStringLiteral("rmmod"),      QStringLiteral("sysctl"),
    QStringLiteral("lvcreate"),   QStringLiteral("vgcreate"),
    QStringLiteral("mdadm"),      QStringLiteral("sh"),
    QStringLiteral("bash"),       QStringLiteral("zsh"),
    QStringLiteral("fish"),       QStringLiteral("dash"),
    QStringLiteral("python"),     QStringLiteral("python3"),
    QStringLiteral("perl"),       QStringLiteral("ruby"),
    QStringLiteral("node"),       QStringLiteral("php"),
    QStringLiteral("lua"),        QStringLiteral("awk"),
    QStringLiteral("sed"),        QStringLiteral("env"),
    QStringLiteral("xargs"),      QStringLiteral("eval"),
    QStringLiteral("source"),     QStringLiteral("exec"),
    QStringLiteral("command"),    QStringLiteral("busybox"),
    QStringLiteral("curl"),       QStringLiteral("wget"),
    QStringLiteral("make"),
    QStringLiteral("cmake"),      QStringLiteral("ninja"),
    QStringLiteral("cat"),        QStringLiteral("tail"),
    QStringLiteral("head"),       QStringLiteral("less"),
    QStringLiteral("more"),       QStringLiteral("tee")
};

const QStringList kProtectedFragments = {
    QStringLiteral("/etc/shadow"),
    QStringLiteral("/etc/gshadow"),
    QStringLiteral("/etc/sudoers"),
    QStringLiteral("/etc/ssl/private"),
    QStringLiteral("/root/"),
    QStringLiteral("/.ssh/"),
    QStringLiteral("/.gnupg/"),
    QStringLiteral("/.aws/"),
    QStringLiteral("/.azure/"),
    QStringLiteral("/.kube/"),
    QStringLiteral("/.docker/config.json"),
    QStringLiteral("/.netrc"),
    QStringLiteral("/.pgpass"),
    QStringLiteral("/.my.cnf"),
    QStringLiteral("id_rsa"),
    QStringLiteral("id_ed25519"),
    QStringLiteral("id_ecdsa"),
    QStringLiteral("credential"),
    QStringLiteral("secret"),
    QStringLiteral("token"),
    QStringLiteral("password"),
    QStringLiteral(".env"),
    QStringLiteral(".pem"),
    QStringLiteral(".key")
};

const QStringList kBlockedTokens = {
    QStringLiteral(";"),     QStringLiteral("|"),
    QStringLiteral("&"),     QStringLiteral("$"),
    QStringLiteral("`"),     QStringLiteral(">"),
    QStringLiteral("<"),     QStringLiteral("\n"),
    QStringLiteral("\r"),    QStringLiteral("$("),
    QStringLiteral("${"),    QStringLiteral("&&"),
    QStringLiteral("||")
};

QString homeRoot()
{
    return QDir::cleanPath(QDir::homePath());
}

QString tmpRoot()
{
    return QDir::cleanPath(QDir::tempPath());
}

bool isPathLike(const QString &value)
{
    return value == QLatin1String(".")
           || value == QLatin1String("..")
           || value.startsWith(QLatin1Char('/'))
           || value.startsWith(QStringLiteral("./"))
           || value.startsWith(QStringLiteral("../"))
           || value.startsWith(QLatin1Char('~'));
}

bool commandUsesRelativePaths(const QString &command)
{
    static const QStringList kPathCommands = {
        QStringLiteral("cp"),    QStringLiteral("mv"),
        QStringLiteral("ln"),    QStringLiteral("mkdir"),
        QStringLiteral("touch"), QStringLiteral("ls"),
        QStringLiteral("stat"),  QStringLiteral("wc"),
        QStringLiteral("du"),
        QStringLiteral("git")
    };
    return kPathCommands.contains(command);
}

QStringList pathCandidatesForArgument(const QString &arg, bool includeRelative)
{
    QStringList candidates;

    if (arg.startsWith(QLatin1Char('-'))) {
        const int eq = arg.indexOf(QLatin1Char('='));
        if (eq > 0) {
            const QString value = arg.mid(eq + 1);
            if (isPathLike(value)) {
                candidates.append(value);
            }
        }
        return candidates;
    }

    if (isPathLike(arg)) {
        candidates.append(arg);
    } else if (includeRelative && !arg.isEmpty()) {
        candidates.append(arg);
    }
    return candidates;
}

} // namespace

QStringList CommandSafetyValidator::lowRiskExecutables()
{
    return kLowRisk;
}

QStringList CommandSafetyValidator::mediumRiskExecutables()
{
    return kMediumRisk;
}

QStringList CommandSafetyValidator::highRiskExecutables()
{
    return kHighRisk;
}

bool CommandSafetyValidator::isAllowedExecutable(const QString &command)
{
    const QString name = command.trimmed();
    return kLowRisk.contains(name) || kMediumRisk.contains(name);
}

bool CommandSafetyValidator::hasShellMetacharacters(const QString &text)
{
    for (const QString &token : kBlockedTokens) {
        if (text.contains(token)) {
            return true;
        }
    }
    return false;
}

bool CommandSafetyValidator::isProtectedPath(const QString &path)
{
    QString normalized = QDir::cleanPath(path).toLower();
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));

    if (!normalized.startsWith(QLatin1Char('/'))) {
        normalized.prepend(QLatin1Char('/'));
    }
    // Append a separator so directory fragments also match when the protected
    // directory itself (for example "~/.ssh") is the final path component.
    if (!normalized.endsWith(QLatin1Char('/'))) {
        normalized.append(QLatin1Char('/'));
    }

    for (const QString &fragment : kProtectedFragments) {
        if (normalized.contains(fragment)) {
            return true;
        }
    }
    return false;
}

QString CommandSafetyValidator::resolveExecutable(const QString &command)
{
    if (command.isEmpty() || command.contains(QLatin1Char('/'))
        || command.contains(QLatin1Char('\\')) || command.contains(QLatin1Char(' '))) {
        return {};
    }

    if (!isAllowedExecutable(command)) {
        return {};
    }

    return QStandardPaths::findExecutable(command);
}

bool CommandSafetyValidator::isWithinPermittedRoot(const QString &absolutePath)
{
    const QString path = QDir::cleanPath(absolutePath);
    const QString home = homeRoot();
    const QString tmp = tmpRoot();

    auto within = [](const QString &candidate, const QString &root) {
        return candidate == root || candidate.startsWith(root + QLatin1Char('/'));
    };

    return within(path, home) || within(path, tmp);
}

QString CommandSafetyValidator::normalizePath(const QString &path,
                                              const QString &workingDirectory)
{
    QString expanded = path.trimmed();
    if (expanded == QLatin1String("~")) {
        expanded = QDir::homePath();
    } else if (expanded.startsWith(QStringLiteral("~/"))) {
        expanded = QDir::homePath() + expanded.mid(1);
    }

    QFileInfo info(expanded);
    QString absolute;
    if (info.isRelative()) {
        const QString base = workingDirectory.isEmpty() ? QDir::homePath()
                                                         : workingDirectory;
        absolute = QDir::cleanPath(QDir(base).filePath(expanded));
    } else {
        absolute = QDir::cleanPath(info.absoluteFilePath());
    }

    // Resolve symlinks for paths that already exist so a link inside the home
    // directory cannot be used to reach a protected location.
    const QFileInfo resolved(absolute);
    const QString canonical = resolved.canonicalFilePath();
    if (!canonical.isEmpty()) {
        return QDir::cleanPath(canonical);
    }
    return absolute;
}

bool CommandSafetyValidator::argumentsContainBlockedToken(const QStringList &arguments,
                                                          QString *reason)
{
    for (const QString &arg : arguments) {
        if (arg.size() > 2048) {
            if (reason) *reason = QStringLiteral("An argument is unreasonably long.");
            return true;
        }
        if (hasShellMetacharacters(arg)) {
            if (reason) {
                *reason = QStringLiteral("Shell operators, substitutions or redirection "
                                         "are not allowed: %1").arg(arg.left(80));
            }
            return true;
        }
    }
    return false;
}

bool CommandSafetyValidator::commandDisallowsArguments(const QString &command,
                                                       const QStringList &arguments,
                                                       QString *reason)
{
    // Following symlinks can escape the permitted roots (for example a link in
    // the home directory pointing at /etc/shadow), so dereference flags are
    // rejected for commands that would read through them.
    if (command == QLatin1String("find") || command == QLatin1String("du")
        || command == QLatin1String("stat") || command == QLatin1String("ls")
        || command == QLatin1String("cp")) {
        for (const QString &arg : arguments) {
            if (arg == QLatin1String("-L") || arg == QLatin1String("-H")
                || arg == QLatin1String("-follow")
                || arg.startsWith(QStringLiteral("--dereference"))) {
                if (reason) {
                    *reason = QStringLiteral("Following symbolic links is not allowed (%1).")
                                  .arg(arg);
                }
                return true;
            }
        }
    }

    if (command == QLatin1String("git")) {
        static const QStringList kAllowedGitSubcommands = {
            QStringLiteral("status"), QStringLiteral("log"), QStringLiteral("diff"),
            QStringLiteral("branch"), QStringLiteral("show"), QStringLiteral("rev-parse")
        };
        static const QStringList kBlockedGitOptions = {
            QStringLiteral("-C"), QStringLiteral("-c"), QStringLiteral("--git-dir"),
            QStringLiteral("--work-tree"), QStringLiteral("--exec-path"),
            QStringLiteral("--ext-diff"), QStringLiteral("--no-index"),
            QStringLiteral("--output"), QStringLiteral("--config-env"),
            QStringLiteral("--config"), QStringLiteral("--super-prefix"),
            QStringLiteral("--namespace"), QStringLiteral("--bare"),
            QStringLiteral("--upload-pack"), QStringLiteral("--receive-pack"),
            QStringLiteral("--exec"), QStringLiteral("--open-files-in-pager")
        };
        static const QStringList kBlockedBranchFlags = {
            QStringLiteral("-d"), QStringLiteral("-D"), QStringLiteral("--delete"),
            QStringLiteral("-m"), QStringLiteral("-M"), QStringLiteral("--move"),
            QStringLiteral("-C"), QStringLiteral("--copy"), QStringLiteral("--set-upstream-to"),
            QStringLiteral("--unset-upstream"), QStringLiteral("-f"), QStringLiteral("--force"),
            QStringLiteral("-u"), QStringLiteral("--track"), QStringLiteral("--edit-description")
        };
        static const QStringList kProtectedGitNames = {
            QStringLiteral(".env"), QStringLiteral(".pem"), QStringLiteral(".key"),
            QStringLiteral("id_rsa"), QStringLiteral("id_ed25519"),
            QStringLiteral("id_ecdsa"), QStringLiteral("credential"),
            QStringLiteral("secret"), QStringLiteral("token"), QStringLiteral("password")
        };

        if (arguments.isEmpty()) {
            if (reason) *reason = QStringLiteral("Git requires a read-only subcommand.");
            return true;
        }
        const QString subcommand = arguments.first();
        if (subcommand.startsWith(QLatin1Char('-'))) {
            if (reason) *reason = QStringLiteral("Global git options are not allowed.");
            return true;
        }
        if (!kAllowedGitSubcommands.contains(subcommand)) {
            if (reason) {
                *reason = QStringLiteral("Only read-only git subcommands are allowed "
                                         "(status, log, diff, branch, show, rev-parse).");
            }
            return true;
        }

        bool branchListing = subcommand == QLatin1String("branch");
        for (int i = 1; i < arguments.size(); ++i) {
            const QString arg = arguments.at(i);
            const QString lower = arg.toLower();
            for (const QString &blocked : kBlockedGitOptions) {
                if (lower == blocked
                    || lower.startsWith(blocked + QLatin1Char('='))) {
                    if (reason) {
                        *reason = QStringLiteral("Git option '%1' is not allowed.").arg(arg);
                    }
                    return true;
                }
            }
            for (const QString &name : kProtectedGitNames) {
                if (lower.contains(name)) {
                    if (reason) {
                        *reason = QStringLiteral("Git arguments must not reference "
                                                 "protected data (%1).").arg(name);
                    }
                    return true;
                }
            }
            if (subcommand == QLatin1String("branch")
                && (kBlockedBranchFlags.contains(arg))) {
                if (reason) {
                    *reason = QStringLiteral("Destructive git branch options are not allowed.");
                }
                return true;
            }
            if (branchListing && !arg.startsWith(QLatin1Char('-'))) {
                if (reason) {
                    *reason = QStringLiteral("Creating or renaming branches is not allowed.");
                }
                return true;
            }
        }
    }

    if (command == QLatin1String("find")) {
        static const QStringList kBlockedFind = {
            QStringLiteral("-exec"), QStringLiteral("-execdir"),
            QStringLiteral("-ok"), QStringLiteral("-okdir"),
            QStringLiteral("-delete"), QStringLiteral("-fprint"),
            QStringLiteral("-fprint0"), QStringLiteral("-fls"),
            QStringLiteral("-writable"), QStringLiteral("-perm")
        };

        for (const QString &arg : arguments) {
            for (const QString &blocked : kBlockedFind) {
                if (arg == blocked
                    || arg.startsWith(blocked + QLatin1Char('='))
                    || arg.startsWith(blocked + QLatin1Char(' '))) {
                    if (reason) {
                        *reason = QStringLiteral("'find %1' could modify files or run "
                                                 "arbitrary programs and is blocked.")
                                      .arg(arg);
                    }
                    return true;
                }
            }
        }
    }

    if (command == QLatin1String("sleep")) {
        for (const QString &arg : arguments) {
            bool ok = false;
            const double seconds = arg.toDouble(&ok);
            if (!ok || seconds < 0.0 || seconds > 300.0) {
                if (reason) {
                    *reason = QStringLiteral("Sleep duration must be a number between 0 and 300.");
                }
                return true;
            }
        }
    }

    return false;
}

TerminalValidationResult CommandSafetyValidator::validate(
    const TerminalCommandProposal &proposal)
{
    TerminalValidationResult result;
    result.requiresSudo = proposal.requiresSudo;

    const QString command = proposal.command.trimmed();

    auto block = [&result](TerminalRisk risk, const QString &reason) {
        result.risk    = risk;
        result.allowed = false;
        result.reason  = reason;
        return result;
    };

    if (!proposal.isValid()) {
        return block(TerminalRisk::Blocked,
                     QStringLiteral("The proposed program name is invalid. Commands must "
                                    "be a single executable name without a path."));
    }

    if (hasShellMetacharacters(command)) {
        return block(TerminalRisk::Blocked,
                     QStringLiteral("The program name contains shell metacharacters."));
    }

    if (proposal.arguments.size() > 64) {
        return block(TerminalRisk::Blocked, QStringLiteral("Too many arguments."));
    }

    if (argumentsContainBlockedToken(proposal.arguments, &result.reason)) {
        result.risk    = TerminalRisk::Blocked;
        result.allowed = false;
        return result;
    }

    if (commandDisallowsArguments(command, proposal.arguments, &result.reason)) {
        result.risk    = TerminalRisk::Blocked;
        result.allowed = false;
        return result;
    }

    // Privilege escalation is always rejected, regardless of the executable list.
    if (proposal.requiresSudo
        || command == QLatin1String("sudo") || command == QLatin1String("su")
        || command == QLatin1String("pkexec") || command == QLatin1String("doas")) {
        result.requiresSudo = true;
        return block(TerminalRisk::High,
                     QStringLiteral("Privilege escalation (sudo/su/pkexec) is not supported. "
                                    "Run the command manually in a terminal if you trust it."));
    }

    if (kHighRisk.contains(command)) {
        return block(TerminalRisk::High,
                     QStringLiteral("'%1' is classified as high risk and is blocked in the "
                                    "initial release. You may copy the command and review it "
                                    "manually.").arg(command));
    }

    if (kMediumRisk.contains(command)) {
        result.risk = TerminalRisk::Medium;
    } else if (kLowRisk.contains(command)) {
        result.risk = TerminalRisk::Low;
    } else {
        return block(TerminalRisk::Blocked,
                     QStringLiteral("'%1' is not in the supported executable allowlist.")
                         .arg(command));
    }

    if (isProtectedPath(command)) {
        return block(TerminalRisk::Blocked,
                     QStringLiteral("The program references a protected path."));
    }

    if (resolveExecutable(command).isEmpty()) {
        return block(TerminalRisk::Blocked,
                     QStringLiteral("The executable '%1' is not installed on this system.")
                         .arg(command));
    }

    QString workingDirectory;
    if (!proposal.workingDirectory.trimmed().isEmpty()) {
        const QString candidate = normalizePath(proposal.workingDirectory, QString());
        const QFileInfo info(candidate);
        if (!info.exists() || !info.isDir()) {
            return block(TerminalRisk::Blocked,
                         QStringLiteral("Working directory does not exist: %1").arg(candidate));
        }
        if (!isWithinPermittedRoot(candidate)) {
            return block(TerminalRisk::Blocked,
                         QStringLiteral("Working directory is outside the permitted roots "
                                        "(home and temporary directories)."));
        }
        workingDirectory = candidate;
    }

    bool findPredicatesStarted = false;
    for (const QString &arg : proposal.arguments) {
        if (command == QLatin1String("find") && arg.startsWith(QLatin1Char('-'))) {
            findPredicatesStarted = true;
        }
        // For find, only the leading path operands are treated as paths; the
        // expressions after the first predicate are patterns/tests.
        const bool includeRelative =
            command == QLatin1String("find") ? !findPredicatesStarted
                                             : commandUsesRelativePaths(command);
        const QStringList candidates = pathCandidatesForArgument(arg, includeRelative);
        for (const QString &candidate : candidates) {
            const QString resolved = normalizePath(candidate, workingDirectory);
            if (isProtectedPath(resolved)) {
                return block(TerminalRisk::Blocked,
                             QStringLiteral("Access to a protected path is not allowed: %1")
                                 .arg(resolved));
            }
            if (!isWithinPermittedRoot(resolved)) {
                return block(TerminalRisk::Blocked,
                             QStringLiteral("Path escapes the permitted directories: %1")
                                 .arg(resolved));
            }
        }
    }

    if (result.risk == TerminalRisk::Medium) {
        result.warnings.append(
            QStringLiteral("This command may create or modify files. Review it carefully."));
    }
    if (command == QLatin1String("cp") || command == QLatin1String("mv")) {
        result.warnings.append(
            QStringLiteral("Existing files could be overwritten. Check the destination."));
    }

    result.allowed = true;
    result.reason.clear();
    return result;
}
