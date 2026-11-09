#ifndef TITANAI_TERMINAL_EXECUTOR_HPP
#define TITANAI_TERMINAL_EXECUTOR_HPP

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

#include "terminal/terminal_types.hpp"

/// Asynchronous, shell-free command executor built on QProcess.
///
/// The executor never invokes sh/bash and never accepts a command string. It
/// re-validates every proposal with CommandSafetyValidator and additionally
/// refuses to run unless the caller explicitly confirms user approval.
class TerminalExecutor : public QObject {
    Q_OBJECT

public:
    explicit TerminalExecutor(QObject *parent = nullptr);
    ~TerminalExecutor() override;

    /// Starts an approved proposal. Returns false when rejected, already busy,
    /// not executable, or userApproved is false.
    bool execute(const TerminalCommandProposal &proposal, bool userApproved);

    void cancel();
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] QString currentCommand() const;
    [[nodiscard]] qint64 elapsedMs() const;

    void setTimeoutMs(int timeoutMs);
    [[nodiscard]] int timeoutMs() const;
    void setMaxOutputBytes(int bytes);
    [[nodiscard]] int maxOutputBytes() const;

signals:
    void stateChanged(bool running);
    void started(const QString &displayCommand);
    void standardOutput(const QString &chunk);
    void standardError(const QString &chunk);
    void outputTruncated();
    void executionRejected(const QString &reason);
    void failedToStart(const QString &error);
    void finished(int exitCode, QProcess::ExitStatus exitStatus,
                  qint64 durationMs, bool timedOut, bool cancelled);

private slots:
    void onReadyReadStdout();
    void onReadyReadStderr();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    void onTimeout();

private:
    void readChannel(bool stdOut);
    void emitChunk(bool stdOut, const QByteArray &data);
    void cleanupProcess();

    QProcess     *m_process{nullptr};
    QTimer        m_timeoutTimer;
    QElapsedTimer m_elapsed;
    QString       m_currentCommand;
    int           m_timeoutMs{30000};
    int           m_maxOutputBytes{512 * 1024};
    int           m_emittedBytes{0};
    bool          m_timedOut{false};
    bool          m_cancelled{false};
    bool          m_truncationAnnounced{false};
    bool          m_finalized{false};
};

#endif // TITANAI_TERMINAL_EXECUTOR_HPP
