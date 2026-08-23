#ifndef TITANAI_DISK_CLEANUP_HPP
#define TITANAI_DISK_CLEANUP_HPP

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QList>
#include <QTimer>

struct MountPointUsage {
    QString device;
    QString mountPoint;
    QString fileSystemType;
    quint64 totalBytes{0};
    quint64 usedBytes{0};
    quint64 freeBytes{0};

    [[nodiscard]] int usedPercent() const
    {
        if (totalBytes == 0) {
            return 0;
        }
        return static_cast<int>((usedBytes * 100ULL) / totalBytes);
    }
};

struct CleanupTarget {
    QString name;
    QString path;
    QString description;
    QString suggestionCommand;
    bool requiresRoot{false};
    quint64 sizeBytes{0};
    int itemCount{0};
    bool sizeKnown{false};
};

class DiskCleanup : public QObject {
    Q_OBJECT

public:
    explicit DiskCleanup(QObject *parent = nullptr);
    ~DiskCleanup() override;

    void startAnalysis();
    void cancelAnalysis();

    [[nodiscard]] bool isAnalyzing() const { return m_analyzing; }
    [[nodiscard]] QList<MountPointUsage> mountPoints() const { return m_mounts; }
    [[nodiscard]] QList<CleanupTarget> cleanupTargets() const { return m_targets; }
    [[nodiscard]] quint64 potentialSavings() const;

    [[nodiscard]] QString formatDiskUsageReport() const;
    [[nodiscard]] QString formatCleanupSuggestions() const;
    [[nodiscard]] QString formatFullReport() const;

    [[nodiscard]] static QString formatBytes(quint64 bytes);

signals:
    void analysisStarted();
    void analysisProgress(const QString &currentItem);
    void analysisFinished(quint64 potentialSavings);
    void analysisError(const QString &error);

private slots:
    void processBatch();
    void onJournalFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onOrphansFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    struct DirTask {
        QString path;
        int depth{0};
        int targetIndex{-1};
    };

    void collectMountPoints();
    void buildScanQueue();
    void startProcessQueries();
    void walkDirectory(const DirTask &task);
    void checkCompletion();
    void finishAnalysis();
    void resetState();
    static CleanupTarget makeTarget(const QString &name, const QString &path,
                                    const QString &description,
                                    const QString &suggestionCommand, bool requiresRoot);
    static quint64 parseJournalUsage(const QString &output);

    QList<MountPointUsage> m_mounts;
    QList<CleanupTarget> m_targets;
    QList<DirTask> m_pendingDirs;

    QProcess *m_journalProcess{nullptr};
    QProcess *m_orphansProcess{nullptr};
    QStringList m_orphanPackages;
    bool m_journalDone{true};
    bool m_orphansDone{true};
    bool m_dirScanDone{true};

    QTimer m_batchTimer;
    int m_entriesVisited{0};
    bool m_analyzing{false};
};

#endif // TITANAI_DISK_CLEANUP_HPP
