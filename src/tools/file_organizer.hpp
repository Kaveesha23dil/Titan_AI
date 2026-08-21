#ifndef TITANAI_FILE_ORGANIZER_HPP
#define TITANAI_FILE_ORGANIZER_HPP

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QTimer>

struct DuplicateGroup {
    QByteArray hash;
    quint64 fileSize{0};
    QStringList filePaths;
    quint64 wastedBytes() const { return fileSize * static_cast<quint64>(qMax(0, filePaths.size() - 1)); }
};

struct FolderSuggestion {
    QString folderName;
    QString description;
    int fileCount{0};
    quint64 totalBytes{0};
    QStringList extensions;
};

class FileOrganizer : public QObject {
    Q_OBJECT

public:
    explicit FileOrganizer(QObject *parent = nullptr);
    ~FileOrganizer() override = default;

    void startScan(const QString &rootPath);
    void cancelScan();

    [[nodiscard]] bool isScanning() const { return m_scanning; }
    [[nodiscard]] QString rootPath() const { return m_rootPath; }
    [[nodiscard]] int filesFound() const { return m_allFiles.size(); }

    [[nodiscard]] QList<DuplicateGroup> duplicateGroups() const { return m_duplicates; }
    [[nodiscard]] QList<FolderSuggestion> suggestedStructure() const { return m_suggestions; }

    [[nodiscard]] QString formatDuplicateReport() const;
    [[nodiscard]] QString formatStructureSuggestion() const;
    [[nodiscard]] QString formatFullReport() const;

    [[nodiscard]] static QString formatBytes(quint64 bytes);
    [[nodiscard]] static QString categoryForExtension(const QString &extension);
    [[nodiscard]] static QString descriptionForCategory(const QString &category);

signals:
    void scanStarted(const QString &rootPath);
    void scanProgress(int filesProcessed, int totalFiles);
    void scanFinished(int filesFound, int duplicateGroupCount, quint64 wastedBytes);
    void scanError(const QString &error);

private slots:
    void processBatch();

private:
    void collectFiles(const QString &directory, int depth);
    void buildHashQueue();
    void hashFile(const QString &filePath);
    void finishScan();
    void resetState();

    QStringList m_allFiles;
    QHash<QString, quint64> m_fileSizes;
    QStringList m_hashQueue;
    int m_hashCursor{0};
    QHash<QString, QByteArray> m_hashes;
    QList<DuplicateGroup> m_duplicates;
    QList<FolderSuggestion> m_suggestions;
    QString m_rootPath;
    QTimer m_batchTimer;
    bool m_scanning{false};
    bool m_truncated{false};
};

#endif // TITANAI_FILE_ORGANIZER_HPP
