#ifndef TITANAI_AUTOMATION_HISTORY_MANAGER_HPP
#define TITANAI_AUTOMATION_HISTORY_MANAGER_HPP

#include <QList>
#include <QObject>
#include <QString>

#include "automation/automation_types.hpp"

/// Persistent, local-only automation history.
///
/// Plans, statuses and short messages are stored; raw terminal output is never
/// persisted so credentials or sensitive command output cannot leak to disk.
class AutomationHistoryManager : public QObject {
    Q_OBJECT

public:
    explicit AutomationHistoryManager(QObject *parent = nullptr);
    ~AutomationHistoryManager() override = default;

    void setStorageDirectory(const QString &directory);
    [[nodiscard]] QString storageDirectory() const;

    QString addEntry(const AutomationHistoryEntry &entry);
    bool updateEntry(const AutomationWorkflowResult &result);
    [[nodiscard]] QList<AutomationHistoryEntry> allEntries() const;
    [[nodiscard]] int count() const;
    void clear();

signals:
    void historyChanged();

private:
    void load();
    void save() const;
    [[nodiscard]] QString storageFilePath() const;
    [[nodiscard]] static AutomationHistoryEntry sanitized(AutomationHistoryEntry entry);

    QString m_storageDir;
    QList<AutomationHistoryEntry> m_entries;
};

#endif // TITANAI_AUTOMATION_HISTORY_MANAGER_HPP
