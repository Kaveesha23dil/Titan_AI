#ifndef TITANAI_TERMINAL_TYPES_HPP
#define TITANAI_TERMINAL_TYPES_HPP

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>

/// Deterministic risk classification for a proposed terminal command.
///
/// The value stored on a proposal after validation is authoritative: it is
/// produced by CommandSafetyValidator, never taken directly from the model.
enum class TerminalRisk {
    Low,
    Medium,
    High,
    Blocked
};

[[nodiscard]] QString terminalRiskName(TerminalRisk risk);
[[nodiscard]] TerminalRisk terminalRiskFromName(const QString &name);

/// A structured command proposal. The model only ever supplies the first set of
/// fields; the `risk`, `executable`, `blockReason` and `warnings` fields are
/// filled in by CommandSafetyValidator and must be treated as authoritative.
struct TerminalCommandProposal {
    // --- Model supplied (untrusted) ---
    QString     command;
    QStringList arguments;
    QString     explanation;
    TerminalRisk modelRisk{TerminalRisk::High};
    bool        requiresSudo{false};
    QString     workingDirectory;

    // --- Validator supplied (authoritative) ---
    TerminalRisk risk{TerminalRisk::High};
    bool         executable{false};
    QString      blockReason;
    QStringList  warnings;

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] QString displayCommand() const;
    [[nodiscard]] QJsonObject toJson() const;
    static TerminalCommandProposal fromJson(const QJsonObject &obj);
};

/// Persistent history record. Contains no terminal output content.
struct TerminalHistoryEntry {
    QString      id;
    QString      request;
    QString      command;
    QString      explanation;
    TerminalRisk risk{TerminalRisk::High};
    QDateTime    timestamp;
    QString      status{QStringLiteral("pending")};
    int          exitCode{-1};
    qint64       durationMs{0};

    [[nodiscard]] QJsonObject toJson() const;
    static TerminalHistoryEntry fromJson(const QJsonObject &obj);
};

#endif // TITANAI_TERMINAL_TYPES_HPP
