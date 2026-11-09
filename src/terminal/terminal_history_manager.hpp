#ifndef TITANAI_TERMINAL_HISTORY_MANAGER_HPP
#define TITANAI_TERMINAL_HISTORY_MANAGER_HPP

#include <QList>
#include <QObject>
#include <QString>

#include "terminal/terminal_types.hpp"

/// Persistent, local-only terminal command history.
///
/// Only metadata is stored: the original request, generated command, short
/// explanation, risk level, timestamp, status, exit code and duration. Terminal
/// output is deliberately never written to disk.
class TerminalHistoryManager : public QObject {
    Q_OBJECT

public:
    explicit TerminalHistoryManager(QObject *parent = nullptr);
    ~TerminalHistoryManager() override = default;

    void setStorageDirectory(const QString &directory);
    [[nodiscard]] QString storageDirectory() const;

    QString addEntry(const TerminalHistoryEntry &entry);
    bool updateEntry(const QString &id, const QString &status,
                     int exitCode = -1, qint64 durationMs = 0);
    [[nodiscard]] QList<TerminalHistoryEntry> allEntries() const;
    [[nodiscard]] int count() const;
    void clear();

signals:
    void historyChanged();

private:
    void load();
    void save() const;
    [[nodiscard]] QString storageFilePath() const;

    QString m_storageDir;
    QList<TerminalHistoryEntry> m_entries;
};

#endif // TITANAI_TERMINAL_HISTORY_MANAGER_HPP
