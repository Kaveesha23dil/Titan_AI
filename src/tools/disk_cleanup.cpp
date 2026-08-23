#include "tools/disk_cleanup.hpp"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#include <algorithm>

namespace {

constexpr int kMaxDepth = 15;
constexpr int kMaxEntries = 150000;
constexpr int kDirsPerBatch = 8;
constexpr int kProgressEvery = 200;
constexpr int kMaxOrphansInReport = 8;

const QStringList kPseudoFileSystems = {
    QStringLiteral("proc"),     QStringLiteral("sysfs"),   QStringLiteral("devfs"),
    QStringLiteral("devtmpfs"), QStringLiteral("tmpfs"),   QStringLiteral("squashfs"),
    QStringLiteral("efivarfs"), QStringLiteral("overlay"), QStringLiteral("cgroup2"),
};

QString executablePath(const QString &name)
{
    return QStandardPaths::findExecutable(name);
}

} // namespace

DiskCleanup::DiskCleanup(QObject *parent)
    : QObject(parent)
{
    m_batchTimer.setInterval(0);
    connect(&m_batchTimer, &QTimer::timeout, this, &DiskCleanup::processBatch);
}

DiskCleanup::~DiskCleanup()
{
    const QList<QProcess *> processes = { m_journalProcess, m_orphansProcess };
    for (QProcess *process : processes) {
        if (process && process->state() != QProcess::NotRunning) {
            process->terminate();
            if (!process->waitForFinished(3000)) {
                process->kill();
            }
        }
    }
}

void DiskCleanup::startAnalysis()
{
    if (m_analyzing) {
        return;
    }

    resetState();
    m_analyzing = true;
    m_dirScanDone = false;

    collectMountPoints();
    emit analysisStarted();

    buildScanQueue();
    startProcessQueries();

    if (m_pendingDirs.isEmpty()) {
        m_dirScanDone = true;
        checkCompletion();
        return;
    }

    m_batchTimer.start();
}

void DiskCleanup::cancelAnalysis()
{
    if (!m_analyzing) {
        return;
    }
    resetState();
}

void DiskCleanup::collectMountPoints()
{
    m_mounts.clear();
    const QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();
    for (const QStorageInfo &volume : volumes) {
        if (!volume.isValid() || !volume.isReady()) {
            continue;
        }
        if (kPseudoFileSystems.contains(volume.fileSystemType())) {
            continue;
        }
        if (volume.device().isEmpty() || !volume.device().startsWith(QLatin1String("/dev"))) {
            continue;
        }

        MountPointUsage usage;
        usage.device = volume.device();
        usage.mountPoint = volume.rootPath();
        usage.fileSystemType = volume.fileSystemType();
        usage.totalBytes = static_cast<quint64>(volume.bytesTotal());
        usage.freeBytes = static_cast<quint64>(volume.bytesFree());
        usage.usedBytes = usage.totalBytes > usage.freeBytes
                              ? usage.totalBytes - usage.freeBytes
                              : 0;
        m_mounts.append(usage);
    }

    std::sort(m_mounts.begin(), m_mounts.end(),
              [](const MountPointUsage &a, const MountPointUsage &b) {
                  return a.usedPercent() > b.usedPercent();
              });
}

void DiskCleanup::buildScanQueue()
{
    m_targets.clear();
    m_pendingDirs.clear();

    const QString home = QDir::homePath();

    struct ScanDef {
        QString name;
        QString path;
        QString description;
        QString suggestionCommand;
        bool requiresRoot;
    };

    const QList<ScanDef> scanDefs = {
        { QStringLiteral("Pacman package cache"),
          QStringLiteral("/var/cache/pacman/pkg"),
          QStringLiteral("Old versions of installed packages kept for rollback"),
          QStringLiteral("sudo paccache -rk1   # keep one version; or: sudo pacman -Sc"),
          true },
        { QStringLiteral("User application cache"),
          home + QStringLiteral("/.cache"),
          QStringLiteral("Cached data from browsers, thumbnails and desktop apps"),
          QStringLiteral("du -sh ~/.cache/* | sort -h   # find the biggest caches, then remove stale ones"),
          false },
        { QStringLiteral("Trash"),
          home + QStringLiteral("/.local/share/Trash"),
          QStringLiteral("Files you moved to the trash but are still on disk"),
          QStringLiteral("Empty the trash from your file manager, or: rm -rf ~/.local/share/Trash/files/*"),
          false },
        { QStringLiteral("Systemd core dumps"),
          QStringLiteral("/var/lib/systemd/coredump"),
          QStringLiteral("Crash dumps recorded by systemd-coredump"),
          QStringLiteral("sudo rm -rf /var/lib/systemd/coredump/*"),
          true },
    };

    int targetIndex = 0;
    for (const ScanDef &def : scanDefs) {
        const QDir dir(def.path);
        if (!dir.exists()) {
            continue;
        }
        m_targets.append(makeTarget(def.name, dir.absolutePath(), def.description,
                                    def.suggestionCommand, def.requiresRoot));
        if (dir.isReadable()) {
            m_pendingDirs.append({ dir.absolutePath(), 0, targetIndex });
        } else {
            m_targets.last().requiresRoot = true;
        }
        ++targetIndex;
    }
}

void DiskCleanup::startProcessQueries()
{
    m_orphanPackages.clear();

    const QString journalctl = executablePath(QStringLiteral("journalctl"));
    if (journalctl.isEmpty()) {
        m_journalDone = true;
    } else {
        m_journalDone = false;
        m_journalProcess = new QProcess(this);
        m_journalProcess->setProgram(journalctl);
        m_journalProcess->setArguments({ QStringLiteral("--disk-usage") });
        connect(m_journalProcess, &QProcess::finished,
                this, &DiskCleanup::onJournalFinished);
        connect(m_journalProcess, &QProcess::errorOccurred, this, [this]() {
            m_journalDone = true;
            checkCompletion();
        });
        m_journalProcess->start();
    }

    const QString pacman = executablePath(QStringLiteral("pacman"));
    if (pacman.isEmpty()) {
        m_orphansDone = true;
    } else {
        m_orphansDone = false;
        m_orphansProcess = new QProcess(this);
        m_orphansProcess->setProgram(pacman);
        m_orphansProcess->setArguments({ QStringLiteral("-Qtdq") });
        connect(m_orphansProcess, &QProcess::finished,
                this, &DiskCleanup::onOrphansFinished);
        connect(m_orphansProcess, &QProcess::errorOccurred, this, [this]() {
            m_orphansDone = true;
            checkCompletion();
        });
        m_orphansProcess->start();
    }

    if (!m_journalDone || !m_orphansDone) {
        m_targets.append(makeTarget(QStringLiteral("Journal logs"),
                                    QStringLiteral("/var/log/journal"),
                                    QStringLiteral("Systemd journal entries accumulated over time"),
                                    QStringLiteral("sudo journalctl --vacuum-size=100M"),
                                    true));
        m_targets.append(makeTarget(QStringLiteral("Orphan packages"),
                                    QString(),
                                    QStringLiteral("Packages installed as dependencies that no other package needs"),
                                    QStringLiteral("sudo pacman -Rns $(pacman -Qtdq)"),
                                    true));
    }
}

void DiskCleanup::processBatch()
{
    if (!m_analyzing) {
        m_batchTimer.stop();
        return;
    }

    int processed = 0;
    while (!m_pendingDirs.isEmpty() && processed < kDirsPerBatch
           && m_entriesVisited < kMaxEntries) {
        const DirTask task = m_pendingDirs.takeLast();
        walkDirectory(task);
        ++processed;
        ++m_entriesVisited;

        if (m_entriesVisited % kProgressEvery == 0) {
            emit analysisProgress(task.path);
        }
    }

    if (m_pendingDirs.isEmpty()) {
        m_batchTimer.stop();
        m_dirScanDone = true;
        checkCompletion();
    }
}

void DiskCleanup::walkDirectory(const DirTask &task)
{
    if (task.depth > kMaxDepth || task.targetIndex < 0
            || task.targetIndex >= m_targets.size()) {
        return;
    }

    CleanupTarget &target = m_targets[task.targetIndex];
    const QDir dir(task.path);
    const QFileInfoList entries =
        dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                          QDir::Name | QDir::DirsFirst);

    for (const QFileInfo &entry : entries) {
        if (entry.isSymLink()) {
            continue;
        }
        if (entry.isDir()) {
            if (m_entriesVisited + m_pendingDirs.size() >= kMaxEntries) {
                break;
            }
            m_pendingDirs.append({ entry.absoluteFilePath(), task.depth + 1, task.targetIndex });
        } else if (entry.isFile()) {
            target.sizeBytes += static_cast<quint64>(entry.size());
            ++target.itemCount;
        }
    }
    target.sizeKnown = true;
}

void DiskCleanup::onJournalFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    m_journalDone = true;
    quint64 journalBytes = 0;
    if (exitStatus == QProcess::NormalExit && exitCode == 0 && m_journalProcess) {
        const QString output = QString::fromLocal8Bit(
            m_journalProcess->readAllStandardOutput());
        journalBytes = parseJournalUsage(output);
    }
    if (m_journalProcess) {
        m_journalProcess->deleteLater();
        m_journalProcess = nullptr;
    }

    for (CleanupTarget &target : m_targets) {
        if (target.name == QLatin1String("Journal logs")) {
            target.sizeBytes = journalBytes;
            target.sizeKnown = journalBytes > 0;
            break;
        }
    }

    checkCompletion();
}

void DiskCleanup::onOrphansFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    m_orphansDone = true;
    if (exitStatus == QProcess::NormalExit && exitCode == 0 && m_orphansProcess) {
        const QString output = QString::fromLocal8Bit(
            m_orphansProcess->readAllStandardOutput());
        for (const QString &line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QString name = line.trimmed();
            if (!name.isEmpty()) {
                m_orphanPackages.append(name);
            }
        }
    }
    if (m_orphansProcess) {
        m_orphansProcess->deleteLater();
        m_orphansProcess = nullptr;
    }

    CleanupTarget *orphanTarget = nullptr;
    for (CleanupTarget &target : m_targets) {
        if (target.name == QLatin1String("Orphan packages")) {
            orphanTarget = &target;
            break;
        }
    }
    if (orphanTarget) {
        orphanTarget->itemCount = m_orphanPackages.size();
        orphanTarget->sizeKnown = !m_orphanPackages.isEmpty();
    }

    checkCompletion();
}

quint64 DiskCleanup::parseJournalUsage(const QString &output)
{
    static const QRegularExpression re(
        QStringLiteral("(\\d+(?:\\.\\d+)?)\\s*([KMGT])"),
        QRegularExpression::CaseInsensitiveOption);

    const QRegularExpressionMatch match = re.match(output);
    if (!match.hasMatch()) {
        return 0;
    }

    bool ok = false;
    const double value = match.captured(1).toDouble(&ok);
    if (!ok) {
        return 0;
    }

    quint64 multiplier = 1;
    const QString unit = match.captured(2).toUpper();
    if (unit == QLatin1String("K")) {
        multiplier = 1024ULL;
    } else if (unit == QLatin1String("M")) {
        multiplier = 1024ULL * 1024ULL;
    } else if (unit == QLatin1String("G")) {
        multiplier = 1024ULL * 1024ULL * 1024ULL;
    } else if (unit == QLatin1String("T")) {
        multiplier = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
    }

    return static_cast<quint64>(value * static_cast<double>(multiplier));
}

void DiskCleanup::checkCompletion()
{
    if (!m_analyzing || !m_dirScanDone || !m_journalDone || !m_orphansDone) {
        return;
    }
    finishAnalysis();
}

void DiskCleanup::finishAnalysis()
{
    m_batchTimer.stop();

    std::sort(m_targets.begin(), m_targets.end(),
              [](const CleanupTarget &a, const CleanupTarget &b) {
                  return a.sizeBytes > b.sizeBytes;
              });

    m_analyzing = false;
    emit analysisFinished(potentialSavings());
}

void DiskCleanup::resetState()
{
    m_batchTimer.stop();
    if (m_journalProcess) {
        m_journalProcess->terminate();
        if (!m_journalProcess->waitForFinished(3000)) {
            m_journalProcess->kill();
        }
        m_journalProcess->deleteLater();
        m_journalProcess = nullptr;
    }
    if (m_orphansProcess) {
        m_orphansProcess->terminate();
        if (!m_orphansProcess->waitForFinished(3000)) {
            m_orphansProcess->kill();
        }
        m_orphansProcess->deleteLater();
        m_orphansProcess = nullptr;
    }

    m_mounts.clear();
    m_targets.clear();
    m_pendingDirs.clear();
    m_orphanPackages.clear();
    m_journalDone = true;
    m_orphansDone = true;
    m_dirScanDone = true;
    m_entriesVisited = 0;
    m_analyzing = false;
}

CleanupTarget DiskCleanup::makeTarget(const QString &name, const QString &path,
                                      const QString &description,
                                      const QString &suggestionCommand, bool requiresRoot)
{
    CleanupTarget target;
    target.name = name;
    target.path = path;
    target.description = description;
    target.suggestionCommand = suggestionCommand;
    target.requiresRoot = requiresRoot;
    return target;
}

quint64 DiskCleanup::potentialSavings() const
{
    quint64 savings = 0;
    for (const CleanupTarget &target : m_targets) {
        if (target.sizeKnown && target.sizeBytes > 0) {
            savings += target.sizeBytes;
        }
    }
    return savings;
}

QString DiskCleanup::formatBytes(quint64 bytes)
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

QString DiskCleanup::formatDiskUsageReport() const
{
    if (m_mounts.isEmpty()) {
        return QStringLiteral("No usable mounted file systems were found.");
    }

    QString report = QStringLiteral("Disk usage overview:\n\n");
    report += QStringLiteral("%1 %2 %3 %4 %5 %6\n")
                  .arg(QStringLiteral("Mount point"), -18)
                  .arg(QStringLiteral("Type"), -8)
                  .arg(QStringLiteral("Total"), -10)
                  .arg(QStringLiteral("Used"), -10)
                  .arg(QStringLiteral("Free"), -10)
                  .arg(QStringLiteral("Use%"), -5);

    for (const MountPointUsage &mount : m_mounts) {
        report += QStringLiteral("%1 %2 %3 %4 %5 %6%\n")
                      .arg(mount.mountPoint, -18)
                      .arg(mount.fileSystemType, -8)
                      .arg(formatBytes(mount.totalBytes), -10)
                      .arg(formatBytes(mount.usedBytes), -10)
                      .arg(formatBytes(mount.freeBytes), -10)
                      .arg(mount.usedPercent());
    }

    const MountPointUsage &busiest = m_mounts.first();
    if (busiest.usedPercent() >= 85) {
        report += QStringLiteral("\nWarning: '%1' is %2% full - cleanup is recommended.\n")
                      .arg(busiest.mountPoint)
                      .arg(busiest.usedPercent());
    } else {
        report += QStringLiteral("\n'%1' (the fullest volume) is %2% full.\n")
                      .arg(busiest.mountPoint)
                      .arg(busiest.usedPercent());
    }

    return report.trimmed();
}

QString DiskCleanup::formatCleanupSuggestions() const
{
    if (m_targets.isEmpty() && m_orphanPackages.isEmpty()) {
        return QStringLiteral("No cleanup opportunities were found. Your system looks tidy!");
    }

    QString report = QStringLiteral("Cleanup suggestions (potential savings: ~%1):\n\n")
                         .arg(formatBytes(potentialSavings()));

    int index = 1;
    for (const CleanupTarget &target : m_targets) {
        QString sizeText;
        if (target.sizeKnown && target.sizeBytes > 0) {
            sizeText = formatBytes(target.sizeBytes);
        } else if (target.name == QLatin1String("Orphan packages") && target.itemCount > 0) {
            sizeText = QStringLiteral("%1 package(s)").arg(target.itemCount);
        } else if (target.requiresRoot) {
            sizeText = QStringLiteral("size unknown (needs root)");
        } else {
            continue;
        }

        report += QStringLiteral("%1) %2 - %3\n")
                      .arg(index++)
                      .arg(target.name)
                      .arg(sizeText);
        report += QStringLiteral("   %1\n").arg(target.description);
        if (!target.path.isEmpty()) {
            report += QStringLiteral("   Path: %1\n").arg(target.path);
        }
        report += QStringLiteral("   Tip : %1\n\n").arg(target.suggestionCommand);
    }

    if (!m_orphanPackages.isEmpty()) {
        report += QStringLiteral("   Orphan packages found:\n");
        const int shown = qMin(m_orphanPackages.size(), kMaxOrphansInReport);
        for (int i = 0; i < shown; ++i) {
            report += QStringLiteral("     - %1\n").arg(m_orphanPackages.at(i));
        }
        if (m_orphanPackages.size() > shown) {
            report += QStringLiteral("     ... and %1 more\n").arg(m_orphanPackages.size() - shown);
        }
        report += QLatin1Char('\n');
    }

    report += QStringLiteral("This is a preview only - TitanAI never deletes files on its own.\n"
                             "Run the suggested commands yourself after reviewing them.");
    return report.trimmed();
}

QString DiskCleanup::formatFullReport() const
{
    QString report;
    report += formatDiskUsageReport();
    report += QStringLiteral("\n\n------------------------------\n\n");
    report += formatCleanupSuggestions();
    return report;
}

