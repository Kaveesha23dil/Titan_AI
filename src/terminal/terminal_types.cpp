#include "terminal/terminal_types.hpp"

#include <QJsonArray>
#include <QJsonDocument>

QString terminalRiskName(TerminalRisk risk)
{
    switch (risk) {
    case TerminalRisk::Low:     return QStringLiteral("low");
    case TerminalRisk::Medium:  return QStringLiteral("medium");
    case TerminalRisk::High:    return QStringLiteral("high");
    case TerminalRisk::Blocked: return QStringLiteral("blocked");
    }
    return QStringLiteral("blocked");
}

TerminalRisk terminalRiskFromName(const QString &name)
{
    const QString lower = name.trimmed().toLower();
    if (lower == QLatin1String("low"))    return TerminalRisk::Low;
    if (lower == QLatin1String("medium")) return TerminalRisk::Medium;
    if (lower == QLatin1String("high"))   return TerminalRisk::High;
    return TerminalRisk::Blocked;
}

bool TerminalCommandProposal::isValid() const
{
    return !command.trimmed().isEmpty()
           && !command.contains(QLatin1Char('/'))
           && !command.contains(QLatin1Char('\\'))
           && !command.contains(QLatin1Char(' '))
           && !command.contains(QLatin1Char('\t'));
}

QString TerminalCommandProposal::displayCommand() const
{
    QStringList parts;
    parts.append(command);

    for (const QString &arg : arguments) {
        if (arg.isEmpty()) {
            parts.append(QStringLiteral("''"));
        } else if (arg.contains(QLatin1Char(' ')) || arg.contains(QLatin1Char('\t'))
                   || arg.contains(QLatin1Char('\'')) || arg.contains(QLatin1Char('"'))) {
            QString quoted = arg;
            quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
            parts.append(QLatin1Char('\'') + quoted + QLatin1Char('\''));
        } else {
            parts.append(arg);
        }
    }

    return parts.join(QLatin1Char(' '));
}

QJsonObject TerminalCommandProposal::toJson() const
{
    QJsonArray argsArray;
    for (const QString &arg : arguments) {
        argsArray.append(arg);
    }

    QJsonObject obj;
    obj[QStringLiteral("command")]       = command;
    obj[QStringLiteral("arguments")]     = argsArray;
    obj[QStringLiteral("explanation")]   = explanation;
    obj[QStringLiteral("risk_level")]    = terminalRiskName(modelRisk);
    obj[QStringLiteral("requires_sudo")] = requiresSudo;
    obj[QStringLiteral("working_directory")] =
        workingDirectory.isEmpty() ? QJsonValue() : QJsonValue(workingDirectory);
    return obj;
}

TerminalCommandProposal TerminalCommandProposal::fromJson(const QJsonObject &obj)
{
    TerminalCommandProposal proposal;
    proposal.command     = obj.value(QStringLiteral("command")).toString().trimmed();
    proposal.explanation = obj.value(QStringLiteral("explanation")).toString().trimmed();
    proposal.modelRisk   = terminalRiskFromName(
        obj.value(QStringLiteral("risk_level")).toString(QStringLiteral("blocked")));
    proposal.requiresSudo = obj.value(QStringLiteral("requires_sudo")).toBool(false);

    const QJsonValue cwd = obj.value(QStringLiteral("working_directory"));
    if (cwd.isString()) {
        proposal.workingDirectory = cwd.toString().trimmed();
    }

    const QJsonValue args = obj.value(QStringLiteral("arguments"));
    if (args.isArray()) {
        const QJsonArray arr = args.toArray();
        for (const QJsonValue &value : arr) {
            if (value.isString()) {
                proposal.arguments.append(value.toString());
            }
        }
    }

    proposal.risk = proposal.modelRisk;
    return proposal;
}

QJsonObject TerminalHistoryEntry::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("id")]          = id;
    obj[QStringLiteral("request")]     = request;
    obj[QStringLiteral("command")]     = command;
    obj[QStringLiteral("explanation")] = explanation;
    obj[QStringLiteral("risk")]        = terminalRiskName(risk);
    obj[QStringLiteral("timestamp")]   = timestamp.toString(Qt::ISODate);
    obj[QStringLiteral("status")]      = status;
    obj[QStringLiteral("exitCode")]    = exitCode;
    obj[QStringLiteral("durationMs")]  = static_cast<double>(durationMs);
    return obj;
}

TerminalHistoryEntry TerminalHistoryEntry::fromJson(const QJsonObject &obj)
{
    TerminalHistoryEntry entry;
    entry.id          = obj.value(QStringLiteral("id")).toString();
    entry.request     = obj.value(QStringLiteral("request")).toString();
    entry.command     = obj.value(QStringLiteral("command")).toString();
    entry.explanation = obj.value(QStringLiteral("explanation")).toString();
    entry.risk        = terminalRiskFromName(
        obj.value(QStringLiteral("risk")).toString(QStringLiteral("high")));
    entry.timestamp   = QDateTime::fromString(
        obj.value(QStringLiteral("timestamp")).toString(), Qt::ISODate);
    entry.status      = obj.value(QStringLiteral("status")).toString(QStringLiteral("pending"));
    entry.exitCode    = obj.value(QStringLiteral("exitCode")).toInt(-1);
    entry.durationMs  = static_cast<qint64>(obj.value(QStringLiteral("durationMs")).toDouble(0));
    return entry;
}
