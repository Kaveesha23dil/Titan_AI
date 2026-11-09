#include "terminal/terminal_executor.hpp"

#include <QDir>
#include <QFileInfo>

#include "terminal/command_safety_validator.hpp"

TerminalExecutor::TerminalExecutor(QObject *parent)
    : QObject(parent)
{
    m_timeoutTimer.setSingleShot(true);
    connect(&m_timeoutTimer, &QTimer::timeout, this, &TerminalExecutor::onTimeout);
}

TerminalExecutor::~TerminalExecutor()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

bool TerminalExecutor::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

QString TerminalExecutor::currentCommand() const
{
    return m_currentCommand;
}

qint64 TerminalExecutor::elapsedMs() const
{
    return m_elapsed.isValid() ? m_elapsed.elapsed() : 0;
}

void TerminalExecutor::setTimeoutMs(int timeoutMs)
{
    m_timeoutMs = qMax(100, timeoutMs);
}

int TerminalExecutor::timeoutMs() const
{
    return m_timeoutMs;
}

void TerminalExecutor::setMaxOutputBytes(int bytes)
{
    m_maxOutputBytes = qMax(4096, bytes);
}

int TerminalExecutor::maxOutputBytes() const
{
    return m_maxOutputBytes;
}

bool TerminalExecutor::execute(const TerminalCommandProposal &proposal, bool userApproved)
{
    if (!userApproved) {
        emit executionRejected(QStringLiteral("Execution requires explicit user approval."));
        return false;
    }

    if (isRunning()) {
        emit executionRejected(QStringLiteral("A command is already running."));
        return false;
    }

    // Re-validate at the execution boundary. The UI may have shown a stale
    // proposal; the executor never trusts its contents.
    TerminalCommandProposal checked = proposal;
    const TerminalValidationResult validation = CommandSafetyValidator::validate(checked);
    checked.risk       = validation.risk;
    checked.executable = validation.allowed;
    checked.blockReason = validation.reason;
    checked.warnings   = validation.warnings;
    checked.requiresSudo = validation.requiresSudo;

    if (!validation.allowed) {
        emit executionRejected(validation.reason.isEmpty()
                                   ? QStringLiteral("The command is not allowed.")
                                   : validation.reason);
        return false;
    }

    const QString program = CommandSafetyValidator::resolveExecutable(checked.command);
    if (program.isEmpty()) {
        emit executionRejected(QStringLiteral("Executable not found: %1").arg(checked.command));
        return false;
    }

    QString workingDirectory = checked.workingDirectory.trimmed();
    if (workingDirectory.isEmpty()) {
        workingDirectory = QDir::homePath();
    }
    const QFileInfo dirInfo(workingDirectory);
    if (!dirInfo.exists() || !dirInfo.isDir()) {
        emit executionRejected(QStringLiteral("Working directory does not exist: %1")
                                   .arg(workingDirectory));
        return false;
    }

    m_currentCommand       = checked.displayCommand();
    m_emittedBytes         = 0;
    m_timedOut             = false;
    m_cancelled            = false;
    m_truncationAnnounced  = false;
    m_finalized            = false;

    m_process = new QProcess(this);
    m_process->setProgram(program);
    m_process->setArguments(checked.arguments);
    m_process->setWorkingDirectory(workingDirectory);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &TerminalExecutor::onReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &TerminalExecutor::onReadyReadStderr);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &TerminalExecutor::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred,
            this, &TerminalExecutor::onProcessError);

    m_elapsed.start();
    m_timeoutTimer.start(m_timeoutMs);

    emit stateChanged(true);
    emit started(m_currentCommand);

    m_process->start();

    return true;
}

void TerminalExecutor::cancel()
{
    if (!isRunning()) {
        return;
    }

    m_cancelled = true;
    m_process->terminate();

    QTimer::singleShot(2000, this, [this]() {
        if (m_process && m_process->state() != QProcess::NotRunning) {
            m_process->kill();
        }
    });
}

void TerminalExecutor::onReadyReadStdout()
{
    readChannel(true);
}

void TerminalExecutor::onReadyReadStderr()
{
    readChannel(false);
}

void TerminalExecutor::readChannel(bool stdOut)
{
    if (!m_process) {
        return;
    }

    const QByteArray data = stdOut ? m_process->readAllStandardOutput()
                                   : m_process->readAllStandardError();
    emitChunk(stdOut, data);
}

void TerminalExecutor::emitChunk(bool stdOut, const QByteArray &data)
{
    if (data.isEmpty()) {
        return;
    }

    const int remaining = m_maxOutputBytes - m_emittedBytes;
    if (remaining <= 0) {
        if (!m_truncationAnnounced) {
            m_truncationAnnounced = true;
            emit outputTruncated();
        }
        return;
    }

    QByteArray chunk = data;
    if (chunk.size() > remaining) {
        chunk = chunk.left(remaining);
        if (!m_truncationAnnounced) {
            m_truncationAnnounced = true;
            emit outputTruncated();
        }
    }

    m_emittedBytes += chunk.size();
    const QString text = QString::fromUtf8(chunk);
    if (stdOut) {
        emit standardOutput(text);
    } else {
        emit standardError(text);
    }
}

void TerminalExecutor::onTimeout()
{
    if (!isRunning()) {
        return;
    }

    m_timedOut = true;
    m_process->kill();
}

void TerminalExecutor::onProcessError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        m_timeoutTimer.stop();
        const QString message = m_process ? m_process->errorString()
                                          : QStringLiteral("Failed to start process.");
        if (!m_finalized) {
            m_finalized = true;
            emit failedToStart(message);
            emit stateChanged(false);
        }
        cleanupProcess();
    }
}

void TerminalExecutor::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_finalized) {
        return;
    }
    m_finalized = true;

    m_timeoutTimer.stop();

    if (m_process) {
        emitChunk(true, m_process->readAllStandardOutput());
        emitChunk(false, m_process->readAllStandardError());
    }

    const qint64 duration = elapsedMs();
    const bool timedOut   = m_timedOut;
    const bool cancelled  = m_cancelled;

    cleanupProcess();

    emit finished(exitCode, exitStatus, duration, timedOut, cancelled);
    emit stateChanged(false);
}

void TerminalExecutor::cleanupProcess()
{
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
}
