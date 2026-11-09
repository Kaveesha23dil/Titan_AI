#ifndef TITANAI_AUTOMATION_TYPES_HPP
#define TITANAI_AUTOMATION_TYPES_HPP

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

enum class AutomationActionType {
    Unknown,
    LaunchApplication,
    FocusWindow,
    ListWindows,
    MoveWindow,
    ResizeWindow,
    OpenDirectory,
    OpenFile,
    GetActiveWindow,
    WaitForWindow,
    RunApprovedTerminalCommand
};

enum class AutomationActionStatus {
    Pending,
    WaitingForApproval,
    Running,
    Completed,
    Failed,
    Cancelled,
    Unsupported,
    Skipped
};

enum class AutomationWorkflowStatus {
    Pending,
    WaitingForApproval,
    Running,
    Completed,
    Failed,
    Cancelled,
    Unsupported
};

[[nodiscard]] QString automationActionTypeName(AutomationActionType type);
[[nodiscard]] AutomationActionType automationActionTypeFromName(const QString &name);
[[nodiscard]] QString automationActionStatusName(AutomationActionStatus status);
[[nodiscard]] AutomationActionStatus automationActionStatusFromName(const QString &name);
[[nodiscard]] QString automationWorkflowStatusName(AutomationWorkflowStatus status);
[[nodiscard]] AutomationWorkflowStatus automationWorkflowStatusFromName(const QString &name);

struct AutomationAction {
    QString              id;
    AutomationActionType type{AutomationActionType::Unknown};
    QString              application;
    QString              windowId;
    QString              path;
    QString              placement;
    QString              command;
    QStringList          arguments;
    QString              description;
    QString              unsupportedReason;
    QStringList          warnings;

    int  x{-1};
    int  y{-1};
    int  width{-1};
    int  height{-1};
    bool hasX{false};
    bool hasY{false};
    bool hasWidth{false};
    bool hasHeight{false};
    bool required{true};
    bool supported{true};
    bool focusExistingIfOpen{true};
    int  timeoutMs{10000};

    [[nodiscard]] QJsonObject toJson() const;
    static AutomationAction fromJson(const QJsonObject &obj);
    [[nodiscard]] QString signature() const;
};

struct AutomationPlan {
    QString                  id;
    QString                  taskName;
    QString                  request;
    QStringList              warnings;
    QList<AutomationAction>  actions;
    bool                     requiresApproval{true};
    bool                     supported{true};
    QString                  fingerprint;
    QDateTime                createdAt;

    [[nodiscard]] QJsonObject toJson() const;
    static AutomationPlan fromJson(const QJsonObject &obj);
    [[nodiscard]] QString computeFingerprint() const;
};

struct AutomationActionResult {
    QString                actionId;
    QString                description;
    AutomationActionStatus status{AutomationActionStatus::Pending};
    QString                message;
    QString                output;
    qint64                 durationMs{0};

    [[nodiscard]] QJsonObject toJson() const;
    static AutomationActionResult fromJson(const QJsonObject &obj);
};

struct AutomationWorkflowResult {
    QString                     workflowId;
    AutomationPlan              plan;
    AutomationWorkflowStatus    status{AutomationWorkflowStatus::Pending};
    QList<AutomationActionResult> results;
    QDateTime                   startedAt;
    QDateTime                   finishedAt;
    QString                     error;
};

struct AutomationHistoryEntry {
    QString                     workflowId;
    QString                     request;
    QString                     taskName;
    AutomationPlan              plan;
    AutomationWorkflowStatus    status{AutomationWorkflowStatus::Pending};
    QDateTime                   startedAt;
    QDateTime                   finishedAt;
    QList<AutomationActionResult> results;
    QString                     error;

    [[nodiscard]] QJsonObject toJson() const;
    static AutomationHistoryEntry fromJson(const QJsonObject &obj);
};

Q_DECLARE_METATYPE(AutomationAction)
Q_DECLARE_METATYPE(AutomationPlan)
Q_DECLARE_METATYPE(AutomationActionResult)
Q_DECLARE_METATYPE(AutomationWorkflowResult)
Q_DECLARE_METATYPE(AutomationActionStatus)
Q_DECLARE_METATYPE(AutomationWorkflowStatus)

#endif // TITANAI_AUTOMATION_TYPES_HPP
