#ifndef TITANAI_TERMINAL_PAGE_HPP
#define TITANAI_TERMINAL_PAGE_HPP

#include <QProcess>
#include <QWidget>

#include "terminal/terminal_types.hpp"

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSplitter;
class QTextBrowser;

class TerminalAssistant;
class TerminalExecutor;
class TerminalHistoryManager;

/// Dedicated "AI Terminal" page: natural-language input, validated command
/// preview, explicit confirmation, live output and local history.
class TerminalPage : public QWidget {
    Q_OBJECT

public:
    explicit TerminalPage(TerminalAssistant *assistant,
                          TerminalExecutor *executor,
                          TerminalHistoryManager *history,
                          QWidget *parent = nullptr);
    ~TerminalPage() override = default;

    /// Used by the chat agent to surface a generated proposal for approval.
    void setPendingProposal(const TerminalCommandProposal &proposal,
                            const QString &request);

signals:
    void statusMessage(const QString &message);

private slots:
    void onGenerateClicked();
    void onExecuteClicked();
    void onCancelClicked();
    void onStopClicked();
    void onCopyCommandClicked();
    void onExplainOutputClicked();
    void onClearHistoryClicked();
    void onHistoryCopyClicked();
    void onHistoryRegenerateClicked();
    void onHistorySelectionChanged();

    void onGenerationStarted(const QString &request);
    void onProposalReady(const TerminalCommandProposal &proposal);
    void onGenerationFailed(const QString &error);

    void onExplanationStarted();
    void onExplanationReady(const QString &explanation);
    void onExplanationFailed(const QString &error);

    void onExecutionStarted(const QString &displayCommand);
    void onStandardOutput(const QString &chunk);
    void onStandardError(const QString &chunk);
    void onOutputTruncated();
    void onExecutionRejected(const QString &reason);
    void onExecutionFailedToStart(const QString &error);
    void onExecutionFinished(int exitCode, QProcess::ExitStatus exitStatus,
                             qint64 durationMs, bool timedOut, bool cancelled);

    void refreshHistory();

private:
    void buildUi();
    void applyStyles();
    void showProposal(const TerminalCommandProposal &proposal);
    void clearProposal();
    void setBusy(bool generating);
    void setRunning(bool running);
    void appendOutput(const QString &text, const QString &color);
    void appendRawOutput(const QString &text);
    void updateStatus(const QString &text, const QString &color);
    static QString riskBadge(const TerminalRisk &risk);
    static QString riskColor(const TerminalRisk &risk);

    TerminalAssistant      *m_assistant{nullptr};
    TerminalExecutor       *m_executor{nullptr};
    TerminalHistoryManager *m_history{nullptr};

    TerminalCommandProposal m_proposal;
    QString                 m_currentRequest;
    QString                 m_pendingHistoryId;
    QString                 m_capturedOutput;
    QString                 m_lastCommand;
    int                     m_lastExitCode{-1};
    qint64                  m_lastDurationMs{0};
    bool                    m_hasProposal{false};
    bool                    m_running{false};
    bool                    m_expectingExplanation{false};

    // Input
    QPlainTextEdit *m_requestEdit{nullptr};
    QPushButton    *m_generateButton{nullptr};
    QProgressBar   *m_loadingBar{nullptr};
    QLabel         *m_inputStatus{nullptr};

    // Preview
    QLabel      *m_commandLabel{nullptr};
    QLabel      *m_explanationLabel{nullptr};
    QLabel      *m_riskLabel{nullptr};
    QLabel      *m_workdirLabel{nullptr};
    QLabel      *m_permissionLabel{nullptr};
    QLabel      *m_warningLabel{nullptr};
    QLabel      *m_approvalNote{nullptr};
    QPushButton *m_copyButton{nullptr};
    QPushButton *m_executeButton{nullptr};
    QPushButton *m_cancelButton{nullptr};

    // Output
    QTextBrowser *m_outputView{nullptr};
    QLabel       *m_outputStatus{nullptr};
    QLabel       *m_exitCodeLabel{nullptr};
    QLabel       *m_durationLabel{nullptr};
    QPushButton  *m_stopButton{nullptr};
    QPushButton  *m_explainButton{nullptr};

    // History
    QListWidget *m_historyList{nullptr};
    QPushButton *m_historyCopyButton{nullptr};
    QPushButton *m_historyRegenerateButton{nullptr};
    QPushButton *m_clearHistoryButton{nullptr};
};

#endif // TITANAI_TERMINAL_PAGE_HPP
