#ifndef TITANAI_TASK_TRACKER_HPP
#define TITANAI_TASK_TRACKER_HPP

#include <QObject>
#include <QTimer>
#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>

struct TaskEntry {
    QString processName;
    QString commandLine;
    QString windowTitle;
    QString category;
    qint64 timestampMs;
    int durationMs{0};
};

class TaskTracker : public QObject {
    Q_OBJECT

public:
    explicit TaskTracker(QObject *parent = nullptr);
    ~TaskTracker() override = default;

    void startTracking();
    void stopTracking();
    [[nodiscard]] bool isTracking() const;

    [[nodiscard]] QList<TaskEntry> recentEntries(int count = 100) const;
    [[nodiscard]] QList<TaskEntry> entriesForDate(const QDate &date) const;
    [[nodiscard]] QJsonArray entriesToJson(const QList<TaskEntry> &entries) const;

    void setTrackingInterval(int intervalMs);
    void setDataFilePath(const QString &path);
    [[nodiscard]] QString dataFilePath() const;

signals:
    void taskDetected(const TaskEntry &entry);
    void trackingError(const QString &error);

private slots:
    void pollProcesses();
    void scanActiveWindow();

private:
    void recordEntry(const TaskEntry &entry);
    void loadHistory();
    void saveHistory();
    QJsonObject entryToJson(const TaskEntry &entry) const;
    TaskEntry jsonToEntry(const QJsonObject &obj) const;
    QString categorizeProcess(const QString &name, const QString &cmdline) const;
    QString detectActiveWindow() const;
    QList<QString> getRunningProcesses() const;
    QString extractProcessName(const QString &rawName) const;
    QString extractCommandLine(const QString &pid) const;
    bool shouldRecord(const QString &processName, const QString &cmdline) const;

    QTimer m_pollTimer;
    QList<TaskEntry> m_entries;
    QString m_dataPath;
    int m_maxEntries{5000};
    QString m_lastProcessName;
    qint64 m_lastProcessStartMs{0};
    bool m_tracking{false};
};

#endif // TITANAI_TASK_TRACKER_HPP
