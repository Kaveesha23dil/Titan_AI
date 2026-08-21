#include "tools/file_organizer.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>

namespace {

constexpr int kMaxDepth = 20;
constexpr int kMaxFiles = 50000;
constexpr int kBatchSize = 16;
constexpr qint64 kReadChunkSize = 64 * 1024;
constexpr quint64 kMaxHashFileSize = 256ULL * 1024ULL * 1024ULL;

const QStringList kSkipDirectories = {
    QStringLiteral("node_modules"),
    QStringLiteral("__pycache__"),
    QStringLiteral("build"),
    QStringLiteral("dist"),
    QStringLiteral("target"),
    QStringLiteral("venv"),
};

const QStringList kDocumentExts = {
    QStringLiteral("pdf"), QStringLiteral("doc"), QStringLiteral("docx"),
    QStringLiteral("odt"), QStringLiteral("rtf"), QStringLiteral("txt"),
    QStringLiteral("md"), QStringLiteral("tex"), QStringLiteral("epub"),
    QStringLiteral("mobi"), QStringLiteral("fb2"), QStringLiteral("pages"),
    QStringLiteral("xls"), QStringLiteral("xlsx"), QStringLiteral("ods"),
    QStringLiteral("ppt"), QStringLiteral("pptx"), QStringLiteral("odp"),
};

const QStringList kImageExts = {
    QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
    QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("svg"),
    QStringLiteral("webp"), QStringLiteral("tiff"), QStringLiteral("tif"),
    QStringLiteral("ico"), QStringLiteral("heic"), QStringLiteral("heif"),
    QStringLiteral("avif"), QStringLiteral("psd"), QStringLiteral("ai"),
    QStringLiteral("eps"), QStringLiteral("raw"), QStringLiteral("cr2"),
};

const QStringList kVideoExts = {
    QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("avi"),
    QStringLiteral("mov"), QStringLiteral("wmv"), QStringLiteral("flv"),
    QStringLiteral("webm"), QStringLiteral("m4v"), QStringLiteral("mpg"),
    QStringLiteral("mpeg"), QStringLiteral("ogv"), QStringLiteral("3gp"),
};

const QStringList kAudioExts = {
    QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("flac"),
    QStringLiteral("ogg"), QStringLiteral("oga"), QStringLiteral("m4a"),
    QStringLiteral("aac"), QStringLiteral("wma"), QStringLiteral("opus"),
    QStringLiteral("mid"), QStringLiteral("midi"),
};

const QStringList kArchiveExts = {
    QStringLiteral("zip"), QStringLiteral("tar"), QStringLiteral("gz"),
    QStringLiteral("bz2"), QStringLiteral("xz"), QStringLiteral("7z"),
    QStringLiteral("rar"), QStringLiteral("zst"), QStringLiteral("tgz"),
    QStringLiteral("tbz"), QStringLiteral("txz"), QStringLiteral("iso"),
    QStringLiteral("dmg"), QStringLiteral("deb"), QStringLiteral("rpm"),
    QStringLiteral("appimage"), QStringLiteral("pkg.tar.zst"),
};

const QStringList kCodeExts = {
    QStringLiteral("c"), QStringLiteral("h"), QStringLiteral("cpp"),
    QStringLiteral("cxx"), QStringLiteral("cc"), QStringLiteral("hpp"),
    QStringLiteral("hxx"), QStringLiteral("cs"), QStringLiteral("java"),
    QStringLiteral("py"), QStringLiteral("rb"), QStringLiteral("rs"),
    QStringLiteral("go"), QStringLiteral("swift"), QStringLiteral("kt"),
    QStringLiteral("scala"), QStringLiteral("php"), QStringLiteral("pl"),
    QStringLiteral("sh"), QStringLiteral("bash"), QStringLiteral("zsh"),
    QStringLiteral("fish"), QStringLiteral("lua"), QStringLiteral("vim"),
    QStringLiteral("js"), QStringLiteral("jsx"), QStringLiteral("ts"),
    QStringLiteral("tsx"), QStringLiteral("vue"), QStringLiteral("svelte"),
    QStringLiteral("html"), QStringLiteral("htm"), QStringLiteral("css"),
    QStringLiteral("scss"), QStringLiteral("less"), QStringLiteral("qml"),
    QStringLiteral("cmake"), QStringLiteral("asm"), QStringLiteral("r"),
    QStringLiteral("jl"), QStringLiteral("dart"),
};

const QStringList kDataExts = {
    QStringLiteral("csv"), QStringLiteral("tsv"), QStringLiteral("json"),
    QStringLiteral("xml"), QStringLiteral("yaml"), QStringLiteral("yml"),
    QStringLiteral("toml"), QStringLiteral("ini"), QStringLiteral("cfg"),
    QStringLiteral("conf"), QStringLiteral("sql"), QStringLiteral("db"),
    QStringLiteral("sqlite"), QStringLiteral("sqlite3"), QStringLiteral("parquet"),
    QStringLiteral("proto"), QStringLiteral("log"),
};

const QStringList kFontExts = {
    QStringLiteral("ttf"), QStringLiteral("otf"), QStringLiteral("woff"),
    QStringLiteral("woff2"), QStringLiteral("eot"),
};

QString trimmedExtension(const QString &fileName)
{
    const int dotIdx = fileName.lastIndexOf(QLatin1Char('.'));
    if (dotIdx == -1 || dotIdx == fileName.size() - 1) {
        return QString();
    }
    return fileName.mid(dotIdx + 1).toLower();
}

} // namespace

FileOrganizer::FileOrganizer(QObject *parent)
    : QObject(parent)
{
    m_batchTimer.setInterval(0);
    connect(&m_batchTimer, &QTimer::timeout, this, &FileOrganizer::processBatch);
}

void FileOrganizer::startScan(const QString &rootPath)
{
    if (m_scanning) {
        return;
    }

    resetState();

    const QDir dir(rootPath);
    if (!dir.exists()) {
        emit scanError(QStringLiteral("Directory does not exist: %1").arg(rootPath));
        return;
    }
    if (!dir.isReadable()) {
        emit scanError(QStringLiteral("Directory is not readable: %1").arg(rootPath));
        return;
    }

    m_rootPath = dir.absolutePath();
    m_scanning = true;
    emit scanStarted(m_rootPath);

    collectFiles(m_rootPath, 0);
    buildHashQueue();

    if (m_hashQueue.isEmpty()) {
        finishScan();
        return;
    }

    m_batchTimer.start();
}

void FileOrganizer::cancelScan()
{
    if (!m_scanning) {
        return;
    }
    resetState();
}

void FileOrganizer::collectFiles(const QString &directory, int depth)
{
    if (m_truncated || depth > kMaxDepth) {
        return;
    }

    const QDir dir(directory);
    const QFileInfoList entries =
        dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::DirsFirst);

    for (const QFileInfo &entry : entries) {
        if (m_allFiles.size() >= kMaxFiles) {
            m_truncated = true;
            return;
        }
        if (entry.isSymLink()) {
            continue;
        }
        if (entry.isDir()) {
            const QString name = entry.fileName();
            if (name.startsWith(QLatin1Char('.'))) {
                continue;
            }
            if (kSkipDirectories.contains(name)) {
                continue;
            }
            collectFiles(entry.absoluteFilePath(), depth + 1);
        } else if (entry.isFile()) {
            m_allFiles.append(entry.absoluteFilePath());
            m_fileSizes.insert(entry.absoluteFilePath(), static_cast<quint64>(entry.size()));
        }
    }
}

void FileOrganizer::buildHashQueue()
{
    QHash<quint64, int> sizeCounts;
    for (auto it = m_fileSizes.constBegin(); it != m_fileSizes.constEnd(); ++it) {
        if (it.value() > 0 && it.value() <= kMaxHashFileSize) {
            sizeCounts[it.value()]++;
        }
    }

    for (const QString &path : std::as_const(m_allFiles)) {
        const quint64 size = m_fileSizes.value(path, 0);
        if (size > 0 && size <= kMaxHashFileSize && sizeCounts.value(size, 0) > 1) {
            m_hashQueue.append(path);
        }
    }
}

void FileOrganizer::processBatch()
{
    if (!m_scanning) {
        m_batchTimer.stop();
        return;
    }

    int processed = 0;
    while (m_hashCursor < m_hashQueue.size() && processed < kBatchSize) {
        hashFile(m_hashQueue.at(m_hashCursor));
        ++m_hashCursor;
        ++processed;
    }

    emit scanProgress(m_hashCursor, m_hashQueue.size());

    if (m_hashCursor >= m_hashQueue.size()) {
        m_batchTimer.stop();
        finishScan();
    }
}

void FileOrganizer::hashFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_hashes.insert(filePath, QByteArray());
        return;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    char buffer[kReadChunkSize];
    while (!file.atEnd()) {
        const qint64 read = file.read(buffer, kReadChunkSize);
        if (read < 0) {
            m_hashes.insert(filePath, QByteArray());
            return;
        }
        hash.addData(QByteArrayView(buffer, read));
    }

    m_hashes.insert(filePath, hash.result());
}

void FileOrganizer::finishScan()
{
    QHash<QByteArray, QStringList> byHash;
    for (auto it = m_hashes.constBegin(); it != m_hashes.constEnd(); ++it) {
        if (!it.value().isEmpty()) {
            byHash[it.value()].append(it.key());
        }
    }

    m_duplicates.clear();
    for (auto it = byHash.constBegin(); it != byHash.constEnd(); ++it) {
        if (it.value().size() > 1) {
            DuplicateGroup group;
            group.hash = it.key();
            group.filePaths = it.value();
            group.filePaths.sort();
            group.fileSize = m_fileSizes.value(group.filePaths.first(), 0);
            m_duplicates.append(group);
        }
    }
    std::sort(m_duplicates.begin(), m_duplicates.end(),
              [](const DuplicateGroup &a, const DuplicateGroup &b) {
                  return a.wastedBytes() > b.wastedBytes();
              });

    struct CategoryAccumulator {
        int fileCount{0};
        quint64 totalBytes{0};
        QSet<QString> extensions;
    };

    QHash<QString, CategoryAccumulator> categories;
    for (const QString &path : std::as_const(m_allFiles)) {
        const QString ext = trimmedExtension(QFileInfo(path).fileName());
        const QString category = categoryForExtension(ext);
        auto &acc = categories[category];
        acc.fileCount++;
        acc.totalBytes += m_fileSizes.value(path, 0);
        if (!ext.isEmpty()) {
            acc.extensions.insert(ext);
        }
    }

    static const QStringList categoryOrder = {
        QStringLiteral("Documents"), QStringLiteral("Images"),
        QStringLiteral("Videos"),    QStringLiteral("Audio"),
        QStringLiteral("Archives"),  QStringLiteral("Code"),
        QStringLiteral("Data"),      QStringLiteral("Fonts"),
        QStringLiteral("Misc"),
    };

    m_suggestions.clear();
    for (const QString &category : categoryOrder) {
        if (!categories.contains(category)) {
            continue;
        }
        const CategoryAccumulator &acc = categories.value(category);
        FolderSuggestion suggestion;
        suggestion.folderName = category;
        suggestion.description = descriptionForCategory(category);
        suggestion.fileCount = acc.fileCount;
        suggestion.totalBytes = acc.totalBytes;
        QStringList exts = acc.extensions.values();
        exts.sort();
        suggestion.extensions = exts.mid(0, 5);
        m_suggestions.append(suggestion);
    }

    m_scanning = false;

    quint64 wasted = 0;
    for (const DuplicateGroup &group : m_duplicates) {
        wasted += group.wastedBytes();
    }
    emit scanFinished(m_allFiles.size(), m_duplicates.size(), wasted);
}

void FileOrganizer::resetState()
{
    m_batchTimer.stop();
    m_allFiles.clear();
    m_fileSizes.clear();
    m_hashQueue.clear();
    m_hashCursor = 0;
    m_hashes.clear();
    m_duplicates.clear();
    m_suggestions.clear();
    m_rootPath.clear();
    m_scanning = false;
    m_truncated = false;
}

QString FileOrganizer::formatBytes(quint64 bytes)
{
    if (bytes >= (1ULL << 30)) {
        return QStringLiteral("%1 GB").arg(QString::number(bytes / static_cast<double>(1ULL << 30), 'f', 2));
    }
    if (bytes >= (1ULL << 20)) {
        return QStringLiteral("%1 MB").arg(QString::number(bytes / static_cast<double>(1ULL << 20), 'f', 1));
    }
    if (bytes >= 1024ULL) {
        return QStringLiteral("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 1));
    }
    return QStringLiteral("%1 B").arg(bytes);
}

QString FileOrganizer::categoryForExtension(const QString &extension)
{
    const QString ext = extension.toLower();

    if (kDocumentExts.contains(ext)) return QStringLiteral("Documents");
    if (kImageExts.contains(ext)) return QStringLiteral("Images");
    if (kVideoExts.contains(ext)) return QStringLiteral("Videos");
    if (kAudioExts.contains(ext)) return QStringLiteral("Audio");
    if (kArchiveExts.contains(ext)) return QStringLiteral("Archives");
    if (kCodeExts.contains(ext)) return QStringLiteral("Code");
    if (kDataExts.contains(ext)) return QStringLiteral("Data");
    if (kFontExts.contains(ext)) return QStringLiteral("Fonts");

    return QStringLiteral("Misc");
}

QString FileOrganizer::descriptionForCategory(const QString &category)
{
    if (category == QLatin1String("Documents")) return QStringLiteral("Documents and office files");
    if (category == QLatin1String("Images")) return QStringLiteral("Pictures and graphics");
    if (category == QLatin1String("Videos")) return QStringLiteral("Video recordings");
    if (category == QLatin1String("Audio")) return QStringLiteral("Music and sound files");
    if (category == QLatin1String("Archives")) return QStringLiteral("Archives and packages");
    if (category == QLatin1String("Code")) return QStringLiteral("Source code");
    if (category == QLatin1String("Data")) return QStringLiteral("Structured data and configs");
    if (category == QLatin1String("Fonts")) return QStringLiteral("Font files");
    return QStringLiteral("Everything else");
}

QString FileOrganizer::formatStructureSuggestion() const
{
    if (m_suggestions.isEmpty()) {
        return QStringLiteral("No files were found in '%1' to organize.").arg(m_rootPath);
    }

    QString report;
    report += QStringLiteral("Suggested folder structure for '%1':\n\n").arg(m_rootPath);
    report += QStringLiteral("TitanAI-Organized/\n");

    const int count = m_suggestions.size();
    for (int i = 0; i < count; ++i) {
        const FolderSuggestion &suggestion = m_suggestions.at(i);
        const QString branch = (i == count - 1)
                                   ? QStringLiteral("\u2514\u2500\u2500")
                                   : QStringLiteral("\u251C\u2500\u2500");
        report += QStringLiteral("%1 %2  %3 (%4)\n")
                      .arg(branch,
                           suggestion.folderName + QLatin1Char('/'),
                           descriptionForCategory(suggestion.folderName),
                           formatBytes(suggestion.totalBytes));
        report += QStringLiteral("     %1 file(s): %2\n")
                      .arg(suggestion.fileCount)
                      .arg(suggestion.extensions.join(QStringLiteral(", ")));
    }

    report += QStringLiteral("\nThis is a preview only - no files were moved.");
    return report;
}

QString FileOrganizer::formatDuplicateReport() const
{
    QString report;
    const QString header = QStringLiteral("Duplicate file scan for '%1' (%2 files checked).")
                               .arg(m_rootPath)
                               .arg(m_allFiles.size());

    if (m_duplicates.isEmpty()) {
        report += header + QStringLiteral("\nNo duplicate files were found. ");
        return report;
    }

    quint64 wasted = 0;
    for (const DuplicateGroup &group : m_duplicates) {
        wasted += group.wastedBytes();
    }

    report += header + QStringLiteral("\nFound %1 duplicate group(s) - wasted space: %2\n\n")
                                 .arg(m_duplicates.size())
                                 .arg(formatBytes(wasted));

    int index = 1;
    for (const DuplicateGroup &group : m_duplicates) {
        report += QStringLiteral("%1) %2 cop(ies), %3 each:\n")
                      .arg(index++)
                      .arg(group.filePaths.size())
                      .arg(formatBytes(group.fileSize));
        for (const QString &path : group.filePaths) {
            report += QStringLiteral("   - %1\n").arg(path);
        }
        report += QLatin1Char('\n');
    }

    return report.trimmed();
}

QString FileOrganizer::formatFullReport() const
{
    QString report;
    report += formatStructureSuggestion();
    report += QStringLiteral("\n\n------------------------------\n\n");
    report += formatDuplicateReport();

    if (m_truncated) {
        report += QStringLiteral("\n\nNote: the scan stopped after reaching the file limit "
                                 "(%1 files); results may be incomplete.")
                      .arg(kMaxFiles);
    }

    return report;
}
