#include "tools/update_checker.hpp"

#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>

namespace {

constexpr int kMaxUpdatesInReport = 40;
constexpr int kNameColumnWidth = -24;
constexpr int kVersionColumnWidth = -20;

QString executablePath(const QString &name)
{
    return QStandardPaths::findExecutable(name);
}

QList<InstalledPackage> parseInstalledPackages(const QString &output)
{
    QList<InstalledPackage> result;
    const QStringList lines = output.split(QLatin1Char('\n'));
    result.reserve(lines.size());

    for (const QString &line : lines) {
        const int spaceIdx = line.indexOf(QLatin1Char(' '));
        if (spaceIdx <= 0) {
            continue;
        }
        InstalledPackage pkg;
        pkg.name = line.left(spaceIdx).trimmed();
        pkg.version = line.mid(spaceIdx + 1).trimmed();
        if (!pkg.name.isEmpty() && !pkg.version.isEmpty()) {
            result.append(pkg);
        }
    }
    return result;
}

bool parseUpdateLine(const QString &line, PendingUpdate &out)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.contains(QLatin1String("[ignored]"))) {
        return false;
    }

    // Format A: "[repo/]name current-version -> new-version"  (pacman -Qu,
    // checkupdates, and AUR helpers all use this shape)
    const QStringList halves = trimmed.split(QStringLiteral(" -> "));
    if (halves.size() == 2) {
        const QStringList left =
            halves.at(0).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        const QStringList right =
            halves.at(1).split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (left.size() < 2 || right.isEmpty()) {
            return false;
        }
        out.name = left.at(left.size() - 2);
        out.currentVersion = left.last();
        out.repository.clear();
        if (out.name.contains(QLatin1Char('/'))) {
            const int slashIdx = out.name.lastIndexOf(QLatin1Char('/'));
            out.repository = out.name.left(slashIdx);
            out.name = out.name.mid(slashIdx + 1);
        }
        out.newVersion = right.first();
        return !out.name.isEmpty();
    }

    // Format B: "repo name current new"  (four plain columns)
    const QStringList tokens =
        trimmed.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (tokens.size() == 4 && !tokens.at(0).contains(QLatin1Char('-'))) {
        out.repository = tokens.at(0);
        out.name = tokens.at(1);
        out.currentVersion = tokens.at(2);
        out.newVersion = tokens.at(3);
        return true;
    }

    return false;
}

} // namespace

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
{
}

UpdateChecker::~UpdateChecker()
{
    const QList<QProcess *> processes = { m_installedProcess, m_repoUpdatesProcess, m_aurProcess };
    for (QProcess *process : processes) {
        if (process && process->state() != QProcess::NotRunning) {
            process->terminate();
            if (!process->waitForFinished(3000)) {
                process->kill();
            }
        }
    }
}

void UpdateChecker::startCheck()
{
    if (m_checking) {
        return;
    }

    resetState();
    m_checking = true;

    emit checkStarted();

    startProcessQueries();

    if (m_installedDone && m_repoUpdatesDone && m_aurDone) {
        finishCheck();
        return;
    }
}

void UpdateChecker::cancelCheck()
{
    if (!m_checking) {
        return;
    }
    resetState();
}

bool UpdateChecker::usesSafeUpdateChecker() const
{
    return m_repoUpdateCommand == QLatin1String("checkupdates");
}

void UpdateChecker::startProcessQueries()
{
    m_installed.clear();
    m_updates.clear();
    m_aurHelper.clear();

    // 1) Installed package versions: "pacman -Q" (name + version per line).
    const QString pacman = executablePath(QStringLiteral("pacman"));
    if (!pacman.isEmpty()) {
        m_installedDone = false;
        m_installedProcess = new QProcess(this);
        m_installedProcess->setProgram(pacman);
        m_installedProcess->setArguments({ QStringLiteral("-Q") });
        connect(m_installedProcess, &QProcess::finished,
                this, &UpdateChecker::onInstalledFinished);
        connect(m_installedProcess, &QProcess::errorOccurred, this, [this]() {
            m_installedDone = true;
            checkCompletion();
        });
        m_installedProcess->start();
    } else {
        emit checkError(QStringLiteral("pacman was not found; TitanAI cannot read "
                                       "installed packages on this system."));
        resetState();
        return;
    }

    // 2) Pending repository updates. Prefer 'checkupdates' (pacman-contrib):
    // it syncs a throwaway copy of the databases as an unprivileged user and
    // never touches the real system state.
    const QString checkupdates = executablePath(QStringLiteral("checkupdates"));
    if (!checkupdates.isEmpty()) {
        m_repoUpdateCommand = QStringLiteral("checkupdates");
    } else {
        // Fallback: compare against the last synced database (may be stale).
        m_repoUpdateCommand = pacman;
    }

    emit checkProgress(QStringLiteral("Reading installed packages..."));

    m_repoUpdatesDone = false;
    m_repoUpdatesProcess = new QProcess(this);
    m_repoUpdatesProcess->setProgram(m_repoUpdateCommand);
    if (m_repoUpdateCommand == QLatin1String("checkupdates")) {
        emit checkProgress(QStringLiteral("Checking repository updates..."));
    } else {
        // Fallback: compare against the last synced database (may be stale).
        m_repoUpdatesProcess->setArguments({ QStringLiteral("-Qu") });
        emit checkProgress(QStringLiteral("Checking repository updates (last synced database)..."));
    }
    connect(m_repoUpdatesProcess, &QProcess::finished,
            this, &UpdateChecker::onRepoUpdatesFinished);
    connect(m_repoUpdatesProcess, &QProcess::errorOccurred, this, [this]() {
        m_repoUpdatesDone = true;
        checkCompletion();
    });
    m_repoUpdatesProcess->start();

    // 3) Pending AUR updates via the user's helper, if one is installed.
    QString aur = executablePath(QStringLiteral("paru"));
    if (aur.isEmpty()) {
        aur = executablePath(QStringLiteral("yay"));
    }
    if (!aur.isEmpty()) {
        m_aurHelper = QFileInfo(aur).fileName();
        m_aurDone = false;
        m_aurProcess = new QProcess(this);
        m_aurProcess->setProgram(aur);
        m_aurProcess->setArguments({ QStringLiteral("-Qua") });
        connect(m_aurProcess, &QProcess::finished,
                this, &UpdateChecker::onAurUpdatesFinished);
        connect(m_aurProcess, &QProcess::errorOccurred, this, [this]() {
            m_aurDone = true;
            checkCompletion();
        });
        m_aurProcess->start();

        emit checkProgress(QStringLiteral("Checking AUR updates..."));
    }
}

void UpdateChecker::onInstalledFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    m_installedDone = true;
    if (exitStatus == QProcess::NormalExit && exitCode == 0 && m_installedProcess) {
        m_installed = parseInstalledPackages(
            QString::fromLocal8Bit(m_installedProcess->readAllStandardOutput()));
    }
    if (m_installedProcess) {
        m_installedProcess->deleteLater();
        m_installedProcess = nullptr;
    }
    checkCompletion();
}

void UpdateChecker::onRepoUpdatesFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    m_repoUpdatesDone = true;
    if (exitStatus == QProcess::NormalExit && exitCode == 0 && m_repoUpdatesProcess) {
        const QStringList lines = QString::fromLocal8Bit(
            m_repoUpdatesProcess->readAllStandardOutput())
                                      .split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            PendingUpdate update;
            if (parseUpdateLine(line, update)) {
                if (update.repository.isEmpty()) {
                    update.repository = QStringLiteral("repo");
                }
                m_updates.append(update);
            }
        }
    }
    if (m_repoUpdatesProcess) {
        m_repoUpdatesProcess->deleteLater();
        m_repoUpdatesProcess = nullptr;
    }
    checkCompletion();
}

void UpdateChecker::onAurUpdatesFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    m_aurDone = true;
    if (exitStatus == QProcess::NormalExit && exitCode == 0 && m_aurProcess) {
        const QStringList lines = QString::fromLocal8Bit(
            m_aurProcess->readAllStandardOutput())
                                      .split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            PendingUpdate update;
            if (parseUpdateLine(line, update)) {
                update.repository = QStringLiteral("AUR");
                m_updates.append(update);
            }
        }
    }
    if (m_aurProcess) {
        m_aurProcess->deleteLater();
        m_aurProcess = nullptr;
    }
    checkCompletion();
}

void UpdateChecker::checkCompletion()
{
    if (!m_checking || !m_installedDone || !m_repoUpdatesDone || !m_aurDone) {
        return;
    }
    finishCheck();
}

void UpdateChecker::finishCheck()
{
    std::stable_sort(m_updates.begin(), m_updates.end(),
                     [](const PendingUpdate &a, const PendingUpdate &b) {
                         if (a.isAur() != b.isAur()) {
                             return a.isAur() < b.isAur(); // official repos first
                         }
                         return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
                     });

    m_checking = false;
    emit checkFinished(m_updates.size());
}

void UpdateChecker::resetState()
{
    for (QProcess **process : { &m_installedProcess, &m_repoUpdatesProcess, &m_aurProcess }) {
        if (*process) {
            if ((*process)->state() != QProcess::NotRunning) {
                (*process)->terminate();
                if (!(*process)->waitForFinished(3000)) {
                    (*process)->kill();
                }
            }
            (*process)->deleteLater();
            *process = nullptr;
        }
    }

    m_installed.clear();
    m_updates.clear();
    m_aurHelper.clear();
    m_repoUpdateCommand.clear();
    m_installedDone = true;
    m_repoUpdatesDone = true;
    m_aurDone = true;
    m_checking = false;
}

QString UpdateChecker::formatUpdateReport() const
{
    const int officialCount = static_cast<int>(std::count_if(
        m_updates.cbegin(), m_updates.cend(),
        [](const PendingUpdate &update) { return !update.isAur(); }));
    const int aurCount = m_updates.size() - officialCount;

    QString report;
    report += QStringLiteral("Installed packages: %1\n\n").arg(m_installed.size());

    if (m_updates.isEmpty()) {
        report += QStringLiteral("All your packages are up to date.");
        return report;
    }

    report += QStringLiteral("Pending updates: %1").arg(m_updates.size());
    if (aurCount > 0) {
        report += QStringLiteral(" (official %1, AUR %2)").arg(officialCount).arg(aurCount);
    }
    report += QStringLiteral(":\n\n");

    const int shown = qMin(m_updates.size(), kMaxUpdatesInReport);
    for (int i = 0; i < shown; ++i) {
        const PendingUpdate &update = m_updates.at(i);
        QString label;
        if (update.isAur()) {
            label = QStringLiteral("[AUR] ") + update.name;
        } else if (update.repository == QLatin1String("repo")) {
            label = update.name;
        } else {
            label = update.repository + QLatin1Char('/') + update.name;
        }
        report += QStringLiteral("  %1 %2 -> %3\n")
                      .arg(label, kNameColumnWidth)
                      .arg(update.currentVersion, kVersionColumnWidth)
                      .arg(update.newVersion);
    }
    if (m_updates.size() > shown) {
        report += QStringLiteral("  ... and %1 more\n").arg(m_updates.size() - shown);
    }

    if (officialCount > 0) {
        report += QStringLiteral("\nTo apply the official repository updates:\n"
                                 "  sudo pacman -Syu");
    }
    if (aurCount > 0) {
        report += QStringLiteral("\n%1To apply AUR updates:\n  %2 -Syu")
                      .arg(officialCount > 0 ? QStringLiteral("\n") : QString())
                      .arg(m_aurHelper.isEmpty() ? QStringLiteral("paru") : m_aurHelper);
    }

    if (!usesSafeUpdateChecker()) {
        report += QStringLiteral(
            "\n\nNote: 'checkupdates' was not found - the result is based on the last "
            "synced database and may be stale.\nInstall 'pacman-contrib' so TitanAI can "
            "check reliably without touching your system state.");
    }

    report += QStringLiteral("\n\nThis is a preview only - TitanAI never installs "
                             "anything on its own. Review the list before updating.");
    return report.trimmed();
}

QString UpdateChecker::formatInstalledSummary() const
{
    return QStringLiteral("%1 package(s) installed on this system.").arg(m_installed.size());
}

