#include "tools/package_manager.hpp"

#include <QProcess>
#include <QStandardPaths>

#include <unistd.h>

namespace {

QString findExecutable(const QString &name)
{
    return QStandardPaths::findExecutable(name);
}

} // namespace

PackageManager::PackageManager(QObject *parent)
    : QObject(parent)
{
}

PackageManager::~PackageManager()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(3000)) {
            m_process->kill();
        }
    }
    delete m_process;
}

bool PackageManager::isBusy() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void PackageManager::install(const QStringList &packages)
{
    if (packages.isEmpty()) {
        emitResult(false, QStringLiteral("No packages specified to install."));
        return;
    }

    if (isBusy()) {
        emitResult(false, QStringLiteral("Another package installation is already in progress."));
        return;
    }

    m_finishedEmitted = false;

    QString pacman = findExecutable(QStringLiteral("pacman"));
    if (pacman.isEmpty()) {
        emitResult(false,
                   QStringLiteral("The 'pacman' package manager was not found. TitanAI "
                                  "currently supports installing packages on Arch Linux."));
        return;
    }

    QStringList args;
    if (geteuid() == 0) {
        args << pacman;
    } else {
        QString pkexec = findExecutable(QStringLiteral("pkexec"));
        if (pkexec.isEmpty()) {
            emitResult(false,
                       QStringLiteral("The 'pkexec' helper (PolicyKit) was not found. "
                                      "Install 'polkit' to allow privileged operations."));
            return;
        }
        args << pkexec << pacman;
    }

    args << QStringLiteral("-S")
         << QStringLiteral("--noconfirm")
         << QStringLiteral("--needed")
         << QStringLiteral("--color=never")
         << packages;

    m_packages = packages;
    m_log.clear();

    m_process = new QProcess(this);
    m_process->setProgram(args.takeFirst());
    m_process->setArguments(args);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &PackageManager::readOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &PackageManager::readOutput);
    connect(m_process, &QProcess::finished, this, &PackageManager::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emitResult(false,
                       QStringLiteral("Failed to start the package manager process. "
                                      "Is 'pacman' (and 'pkexec' if not running as root) installed?"));
        }
    });

    m_process->start();
}

void PackageManager::readOutput()
{
    if (!m_process) {
        return;
    }

    QByteArray data = m_process->readAllStandardOutput();
    data += m_process->readAllStandardError();

    if (data.isEmpty()) {
        return;
    }

    m_log.append(data);

    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray &rawLine : lines) {
        QString line = QString::fromUtf8(rawLine).trimmed();
        if (!line.isEmpty()) {
            emit outputReceived(line);
        }
    }
}

void PackageManager::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }

    if (exitStatus == QProcess::CrashExit || exitCode != 0) {
        emitResult(false, buildSummary(false));
        return;
    }

    emitResult(true, buildSummary(true));
}

void PackageManager::emitResult(bool success, const QString &summary)
{
    if (m_finishedEmitted) {
        return;
    }
    m_finishedEmitted = true;
    emit finished(success, summary);
}

QString PackageManager::buildSummary(bool success) const
{
    QString joined = m_packages.join(QStringLiteral(", "));

    if (success) {
        return QStringLiteral("Successfully installed %1.").arg(joined);
    }

    QString details;
    const QList<QByteArray> lines = m_log.split('\n');
    QStringList nonEmpty;
    for (const QByteArray &raw : lines) {
        QString line = QString::fromUtf8(raw).trimmed();
        if (!line.isEmpty()) {
            nonEmpty.append(line);
        }
    }

    constexpr int kTailLines = 5;
    const int startIdx = qMax(0, nonEmpty.size() - kTailLines);
    const QStringList tail = nonEmpty.mid(startIdx);

    if (!tail.isEmpty()) {
        details = QStringLiteral("\n\n") + tail.join(QStringLiteral("\n"));
    }

    return QStringLiteral("Installation of %1 failed. Please check the output below.%2")
        .arg(joined, details);
}
