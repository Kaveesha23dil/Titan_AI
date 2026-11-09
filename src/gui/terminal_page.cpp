#include "gui/terminal_page.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextCursor>
#include <QVBoxLayout>

#include "terminal/command_safety_validator.hpp"
#include "terminal/terminal_assistant.hpp"
#include "terminal/terminal_executor.hpp"
#include "terminal/terminal_history_manager.hpp"

namespace TP {
constexpr auto BgDeep      = "#0b0f19";
constexpr auto BgCard      = "#111827";
constexpr auto BgCardHover = "#1a2236";
constexpr auto BgInput     = "#0f1629";
constexpr auto Border      = "#1e293b";
constexpr auto BorderLight = "#2a3a52";
constexpr auto Accent      = "#6366f1";
constexpr auto AccentGlow  = "#818cf8";
constexpr auto AccentCyan  = "#22d3ee";
constexpr auto Success     = "#22c55e";
constexpr auto TextPrimary = "#f1f5f9";
constexpr auto TextSec     = "#94a3b8";
constexpr auto TextMuted   = "#64748b";
constexpr auto Danger      = "#ef4444";
constexpr auto Warning     = "#f59e0b";
}

namespace {

constexpr int kMaxCapturedChars = 200000;

QString cardStyle()
{
    return QStringLiteral(
        "QFrame#terminalCard { background: %1; border: 1px solid %2; border-radius: 10px; }")
        .arg(TP::BgCard, TP::Border);
}

QString sectionTitleStyle()
{
    return QStringLiteral(
        "QLabel { color: %1; font-size: 12px; font-weight: 700; "
        "letter-spacing: 0.5px; background: transparent; border: none; }")
        .arg(TP::TextSec);
}

QString buttonStyle()
{
    return QStringLiteral(
        "QPushButton { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 7px; padding: 7px 14px; font-size: 12px; font-weight: 600; } "
        "QPushButton:hover { background: %4; border-color: %5; } "
        "QPushButton:disabled { color: %6; background: %7; border-color: %8; }")
        .arg(TP::BgInput, TP::TextPrimary, TP::Border, TP::BgCardHover,
             TP::Accent, TP::TextMuted, TP::BgCard, TP::Border);
}

QString primaryButtonStyle()
{
    return QStringLiteral(
        "QPushButton { background: %1; color: white; border: none; border-radius: 7px; "
        "padding: 8px 16px; font-size: 12px; font-weight: 700; } "
        "QPushButton:hover { background: %2; } "
        "QPushButton:disabled { background: %3; color: %4; }")
        .arg(TP::Accent, TP::AccentGlow, TP::BgInput, TP::TextMuted);
}

QString dangerButtonStyle()
{
    return QStringLiteral(
        "QPushButton { background: transparent; color: %1; border: 1px solid %1; "
        "border-radius: 7px; padding: 7px 14px; font-size: 12px; font-weight: 600; } "
        "QPushButton:hover { background: rgba(239,68,68,0.12); } "
        "QPushButton:disabled { color: %2; border-color: %3; }")
        .arg(TP::Danger, TP::TextMuted, TP::Border);
}

} // namespace

TerminalPage::TerminalPage(TerminalAssistant *assistant,
                           TerminalExecutor *executor,
                           TerminalHistoryManager *history,
                           QWidget *parent)
    : QWidget(parent)
    , m_assistant(assistant)
    , m_executor(executor)
    , m_history(history)
{
    buildUi();
    applyStyles();

    if (m_assistant) {
        connect(m_assistant, &TerminalAssistant::generationStarted,
                this, &TerminalPage::onGenerationStarted);
        connect(m_assistant, &TerminalAssistant::proposalReady,
                this, &TerminalPage::onProposalReady);
        connect(m_assistant, &TerminalAssistant::generationFailed,
                this, &TerminalPage::onGenerationFailed);
        connect(m_assistant, &TerminalAssistant::explanationStarted,
                this, &TerminalPage::onExplanationStarted);
        connect(m_assistant, &TerminalAssistant::explanationReady,
                this, &TerminalPage::onExplanationReady);
        connect(m_assistant, &TerminalAssistant::explanationFailed,
                this, &TerminalPage::onExplanationFailed);
    }

    if (m_executor) {
        connect(m_executor, &TerminalExecutor::started,
                this, &TerminalPage::onExecutionStarted);
        connect(m_executor, &TerminalExecutor::standardOutput,
                this, &TerminalPage::onStandardOutput);
        connect(m_executor, &TerminalExecutor::standardError,
                this, &TerminalPage::onStandardError);
        connect(m_executor, &TerminalExecutor::outputTruncated,
                this, &TerminalPage::onOutputTruncated);
        connect(m_executor, &TerminalExecutor::executionRejected,
                this, &TerminalPage::onExecutionRejected);
        connect(m_executor, &TerminalExecutor::failedToStart,
                this, &TerminalPage::onExecutionFailedToStart);
        connect(m_executor, &TerminalExecutor::finished,
                this, &TerminalPage::onExecutionFinished);
    }

    if (m_history) {
        connect(m_history, &TerminalHistoryManager::historyChanged,
                this, &TerminalPage::refreshHistory);
        refreshHistory();
    }
}

void TerminalPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(10);

    auto *titleRow = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("🖥️  AI Terminal"), this);
    title->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 18px; font-weight: 800; background: transparent; }")
        .arg(TP::TextPrimary));
    titleRow->addWidget(title);
    auto *subtitle = new QLabel(
        QStringLiteral("Describe a Linux task. TitanAI proposes a command; you approve every execution."),
        this);
    subtitle->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; background: transparent; }").arg(TP::TextMuted));
    titleRow->addSpacing(10);
    titleRow->addWidget(subtitle);
    titleRow->addStretch();
    root->addLayout(titleRow);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    // ── Left: input, preview, output ──────────────────────────────
    auto *left = new QWidget(splitter);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(10);

    // Input card
    auto *inputCard = new QFrame(left);
    inputCard->setObjectName(QStringLiteral("terminalCard"));
    auto *inputLayout = new QVBoxLayout(inputCard);
    inputLayout->setContentsMargins(14, 12, 14, 12);
    inputLayout->setSpacing(8);

    auto *inputTitle = new QLabel(QStringLiteral("NATURAL LANGUAGE REQUEST"), inputCard);
    inputTitle->setStyleSheet(sectionTitleStyle());
    inputLayout->addWidget(inputTitle);

    m_requestEdit = new QPlainTextEdit(inputCard);
    m_requestEdit->setPlaceholderText(
        QStringLiteral("e.g. Find the 5 largest files in my Downloads folder"));
    m_requestEdit->setFixedHeight(64);
    m_requestEdit->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 7px; padding: 8px; font-size: 13px; } "
        "QPlainTextEdit:focus { border-color: %4; }")
        .arg(TP::BgInput, TP::TextPrimary, TP::Border, TP::Accent));
    inputLayout->addWidget(m_requestEdit);

    auto *inputRow = new QHBoxLayout;
    m_generateButton = new QPushButton(QStringLiteral("✨ Generate Command"), inputCard);
    m_generateButton->setStyleSheet(primaryButtonStyle());
    m_generateButton->setCursor(Qt::PointingHandCursor);
    connect(m_generateButton, &QPushButton::clicked,
            this, &TerminalPage::onGenerateClicked);
    inputRow->addWidget(m_generateButton);

    m_loadingBar = new QProgressBar(inputCard);
    m_loadingBar->setRange(0, 0);
    m_loadingBar->setTextVisible(false);
    m_loadingBar->setFixedHeight(4);
    m_loadingBar->setFixedWidth(120);
    m_loadingBar->setVisible(false);
    inputRow->addWidget(m_loadingBar);

    m_inputStatus = new QLabel(inputCard);
    m_inputStatus->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; background: transparent; }").arg(TP::TextMuted));
    inputRow->addWidget(m_inputStatus, 1);
    inputLayout->addLayout(inputRow);

    leftLayout->addWidget(inputCard);

    // Preview card
    auto *previewCard = new QFrame(left);
    previewCard->setObjectName(QStringLiteral("terminalCard"));
    auto *previewLayout = new QVBoxLayout(previewCard);
    previewLayout->setContentsMargins(14, 12, 14, 12);
    previewLayout->setSpacing(8);

    auto *previewHeader = new QHBoxLayout;
    auto *previewTitle = new QLabel(QStringLiteral("COMMAND PREVIEW"), previewCard);
    previewTitle->setStyleSheet(sectionTitleStyle());
    previewHeader->addWidget(previewTitle);
    previewHeader->addStretch();

    m_riskLabel = new QLabel(previewCard);
    m_riskLabel->setAlignment(Qt::AlignCenter);
    m_riskLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 10px; font-weight: 700; background: transparent; }")
        .arg(TP::TextMuted));
    previewHeader->addWidget(m_riskLabel);
    previewLayout->addLayout(previewHeader);

    m_commandLabel = new QLabel(previewCard);
    m_commandLabel->setWordWrap(true);
    m_commandLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_commandLabel->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_commandLabel->setStyleSheet(QStringLiteral(
        "QLabel { background: %1; color: %2; border: 1px solid %3; border-radius: 7px; "
        "padding: 10px; font-size: 13px; }")
        .arg(TP::BgDeep, TP::AccentCyan, TP::Border));
    m_commandLabel->setText(QStringLiteral("No command proposed yet."));
    previewLayout->addWidget(m_commandLabel);

    m_explanationLabel = new QLabel(previewCard);
    m_explanationLabel->setWordWrap(true);
    m_explanationLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 12px; background: transparent; }")
        .arg(TP::TextSec));
    previewLayout->addWidget(m_explanationLabel);

    auto *detailsRow = new QHBoxLayout;
    m_workdirLabel = new QLabel(previewCard);
    m_permissionLabel = new QLabel(previewCard);
    for (QLabel *label : {m_workdirLabel, m_permissionLabel}) {
        label->setWordWrap(true);
        label->setStyleSheet(QStringLiteral(
            "QLabel { color: %1; font-size: 11px; background: transparent; }")
            .arg(TP::TextMuted));
    }
    detailsRow->addWidget(m_workdirLabel, 1);
    detailsRow->addWidget(m_permissionLabel, 1);
    previewLayout->addLayout(detailsRow);

    m_warningLabel = new QLabel(previewCard);
    m_warningLabel->setWordWrap(true);
    m_warningLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; background: rgba(245,158,11,0.08); "
        "border: 1px solid rgba(245,158,11,0.35); border-radius: 6px; padding: 6px 8px; }")
        .arg(TP::Warning));
    m_warningLabel->setVisible(false);
    previewLayout->addWidget(m_warningLabel);

    m_approvalNote = new QLabel(
        QStringLiteral("Execution requires your explicit approval."), previewCard);
    m_approvalNote->setWordWrap(true);
    m_approvalNote->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; background: transparent; }").arg(TP::TextMuted));
    previewLayout->addWidget(m_approvalNote);

    auto *previewButtons = new QHBoxLayout;
    m_copyButton = new QPushButton(QStringLiteral("📋 Copy Command"), previewCard);
    m_copyButton->setStyleSheet(buttonStyle());
    m_copyButton->setCursor(Qt::PointingHandCursor);
    connect(m_copyButton, &QPushButton::clicked,
            this, &TerminalPage::onCopyCommandClicked);
    previewButtons->addWidget(m_copyButton);
    previewButtons->addStretch();

    m_cancelButton = new QPushButton(QStringLiteral("Cancel"), previewCard);
    m_cancelButton->setStyleSheet(buttonStyle());
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    connect(m_cancelButton, &QPushButton::clicked,
            this, &TerminalPage::onCancelClicked);
    previewButtons->addWidget(m_cancelButton);

    m_executeButton = new QPushButton(QStringLiteral("▶ Execute"), previewCard);
    m_executeButton->setStyleSheet(primaryButtonStyle());
    m_executeButton->setCursor(Qt::PointingHandCursor);
    m_executeButton->setEnabled(false);
    connect(m_executeButton, &QPushButton::clicked,
            this, &TerminalPage::onExecuteClicked);
    previewButtons->addWidget(m_executeButton);
    previewLayout->addLayout(previewButtons);

    leftLayout->addWidget(previewCard);

    // Output card
    auto *outputCard = new QFrame(left);
    outputCard->setObjectName(QStringLiteral("terminalCard"));
    auto *outputLayout = new QVBoxLayout(outputCard);
    outputLayout->setContentsMargins(14, 12, 14, 12);
    outputLayout->setSpacing(7);

    auto *outputHeader = new QHBoxLayout;
    auto *outputTitle = new QLabel(QStringLiteral("TERMINAL OUTPUT"), outputCard);
    outputTitle->setStyleSheet(sectionTitleStyle());
    outputHeader->addWidget(outputTitle);
    outputHeader->addStretch();

    m_explainButton = new QPushButton(QStringLiteral("🤖 Explain Output"), outputCard);
    m_explainButton->setStyleSheet(buttonStyle());
    m_explainButton->setCursor(Qt::PointingHandCursor);
    m_explainButton->setEnabled(false);
    connect(m_explainButton, &QPushButton::clicked,
            this, &TerminalPage::onExplainOutputClicked);
    outputHeader->addWidget(m_explainButton);

    m_stopButton = new QPushButton(QStringLiteral("■ Stop Execution"), outputCard);
    m_stopButton->setStyleSheet(dangerButtonStyle());
    m_stopButton->setCursor(Qt::PointingHandCursor);
    m_stopButton->setEnabled(false);
    connect(m_stopButton, &QPushButton::clicked,
            this, &TerminalPage::onStopClicked);
    outputHeader->addWidget(m_stopButton);
    outputLayout->addLayout(outputHeader);

    m_outputView = new QTextBrowser(outputCard);
    m_outputView->setReadOnly(true);
    m_outputView->setOpenExternalLinks(false);
    m_outputView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_outputView->setMinimumHeight(140);
    m_outputView->setStyleSheet(QStringLiteral(
        "QTextBrowser { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 7px; padding: 8px; font-size: 12px; }")
        .arg(TP::BgDeep, TP::TextPrimary, TP::Border));
    outputLayout->addWidget(m_outputView, 1);

    auto *outputStatusRow = new QHBoxLayout;
    m_outputStatus = new QLabel(QStringLiteral("Idle"), outputCard);
    m_outputStatus->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; font-weight: 600; background: transparent; }")
        .arg(TP::TextMuted));
    m_exitCodeLabel = new QLabel(QStringLiteral("Exit code: —"), outputCard);
    m_durationLabel = new QLabel(QStringLiteral("Duration: —"), outputCard);
    for (QLabel *label : {m_exitCodeLabel, m_durationLabel}) {
        label->setStyleSheet(QStringLiteral(
            "QLabel { color: %1; font-size: 11px; background: transparent; }")
            .arg(TP::TextMuted));
    }
    outputStatusRow->addWidget(m_outputStatus);
    outputStatusRow->addStretch();
    outputStatusRow->addWidget(m_exitCodeLabel);
    outputStatusRow->addSpacing(14);
    outputStatusRow->addWidget(m_durationLabel);
    outputLayout->addLayout(outputStatusRow);

    leftLayout->addWidget(outputCard, 1);

    // ── Right: history ────────────────────────────────────────────
    auto *right = new QWidget(splitter);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);

    auto *historyCard = new QFrame(right);
    historyCard->setObjectName(QStringLiteral("terminalCard"));
    auto *historyLayout = new QVBoxLayout(historyCard);
    historyLayout->setContentsMargins(12, 12, 12, 12);
    historyLayout->setSpacing(8);

    auto *historyHeader = new QHBoxLayout;
    auto *historyTitle = new QLabel(QStringLiteral("COMMAND HISTORY"), historyCard);
    historyTitle->setStyleSheet(sectionTitleStyle());
    historyHeader->addWidget(historyTitle);
    historyHeader->addStretch();
    m_clearHistoryButton = new QPushButton(QStringLiteral("Clear"), historyCard);
    m_clearHistoryButton->setStyleSheet(buttonStyle());
    m_clearHistoryButton->setCursor(Qt::PointingHandCursor);
    connect(m_clearHistoryButton, &QPushButton::clicked,
            this, &TerminalPage::onClearHistoryClicked);
    historyHeader->addWidget(m_clearHistoryButton);
    historyLayout->addLayout(historyHeader);

    m_historyList = new QListWidget(historyCard);
    m_historyList->setStyleSheet(QStringLiteral(
        "QListWidget { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 7px; padding: 4px; font-size: 11px; } "
        "QListWidget::item { padding: 6px; border-bottom: 1px solid %3; } "
        "QListWidget::item:selected { background: %4; color: %5; }")
        .arg(TP::BgDeep, TP::TextSec, TP::Border, TP::BgCardHover, TP::TextPrimary));
    connect(m_historyList, &QListWidget::itemSelectionChanged,
            this, &TerminalPage::onHistorySelectionChanged);
    connect(m_historyList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem *) { onHistoryRegenerateClicked(); });
    historyLayout->addWidget(m_historyList, 1);

    auto *historyButtons = new QHBoxLayout;
    m_historyCopyButton = new QPushButton(QStringLiteral("Copy"), historyCard);
    m_historyCopyButton->setStyleSheet(buttonStyle());
    m_historyCopyButton->setCursor(Qt::PointingHandCursor);
    m_historyCopyButton->setEnabled(false);
    connect(m_historyCopyButton, &QPushButton::clicked,
            this, &TerminalPage::onHistoryCopyClicked);
    historyButtons->addWidget(m_historyCopyButton);

    m_historyRegenerateButton = new QPushButton(QStringLiteral("Regenerate"), historyCard);
    m_historyRegenerateButton->setStyleSheet(buttonStyle());
    m_historyRegenerateButton->setCursor(Qt::PointingHandCursor);
    m_historyRegenerateButton->setEnabled(false);
    connect(m_historyRegenerateButton, &QPushButton::clicked,
            this, &TerminalPage::onHistoryRegenerateClicked);
    historyButtons->addWidget(m_historyRegenerateButton);
    historyLayout->addLayout(historyButtons);

    rightLayout->addWidget(historyCard, 1);

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({640, 320});

    root->addWidget(splitter, 1);
}

void TerminalPage::applyStyles()
{
    setStyleSheet(QStringLiteral("TerminalPage { background: %1; }")
                      .arg(TP::BgDeep)
                  + cardStyle());
}

void TerminalPage::setPendingProposal(const TerminalCommandProposal &proposal,
                                      const QString &request)
{
    m_currentRequest = request;
    m_requestEdit->setPlainText(request);
    showProposal(proposal);
    updateStatus(QStringLiteral("Command proposal ready. Review it and approve execution."),
                 TP::Warning);
}

void TerminalPage::showProposal(const TerminalCommandProposal &proposal)
{
    m_proposal = proposal;
    m_hasProposal = true;

    m_commandLabel->setText(proposal.displayCommand().isEmpty()
                                ? QStringLiteral("No command proposed yet.")
                                : proposal.displayCommand());
    m_explanationLabel->setText(proposal.explanation);
    m_riskLabel->setText(riskBadge(proposal.risk));
    m_riskLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 10px; font-weight: 700; background: transparent; }")
        .arg(riskColor(proposal.risk)));

    m_workdirLabel->setText(
        QStringLiteral("Working directory: %1")
            .arg(proposal.workingDirectory.isEmpty() ? QStringLiteral("Home (default)")
                                                     : proposal.workingDirectory));
    m_permissionLabel->setText(
        proposal.requiresSudo
            ? QStringLiteral("Permissions: elevated privileges requested — blocked")
            : QStringLiteral("Permissions: no elevation required"));

    QStringList warnings = proposal.warnings;
    if (!proposal.executable && !proposal.blockReason.isEmpty()) {
        warnings.prepend(proposal.blockReason);
    }
    if (warnings.isEmpty()) {
        m_warningLabel->setVisible(false);
    } else {
        m_warningLabel->setText(warnings.join(QStringLiteral("\n")));
        m_warningLabel->setVisible(true);
    }

    m_executeButton->setEnabled(proposal.executable && !m_running);
    m_copyButton->setEnabled(!proposal.displayCommand().isEmpty());
    m_cancelButton->setEnabled(true);

    if (!proposal.executable) {
        m_approvalNote->setText(
            QStringLiteral("⛔ Execution is disabled for this proposal. "
                           "You can copy the command and review it manually."));
        m_approvalNote->setStyleSheet(QStringLiteral(
            "QLabel { color: %1; font-size: 11px; background: transparent; }")
            .arg(TP::Danger));
        updateStatus(QStringLiteral("Command blocked by the safety validator."), TP::Danger);
    } else {
        m_approvalNote->setText(
            QStringLiteral("Execution requires your explicit approval. "
                           "TitanAI will never run this command automatically."));
        m_approvalNote->setStyleSheet(QStringLiteral(
            "QLabel { color: %1; font-size: 11px; background: transparent; }")
            .arg(TP::TextMuted));
    }
}

void TerminalPage::clearProposal()
{
    m_proposal = TerminalCommandProposal();
    m_hasProposal = false;
    m_commandLabel->setText(QStringLiteral("No command proposed yet."));
    m_explanationLabel->clear();
    m_riskLabel->clear();
    m_workdirLabel->clear();
    m_permissionLabel->clear();
    m_warningLabel->clear();
    m_warningLabel->setVisible(false);
    m_executeButton->setEnabled(false);
    m_copyButton->setEnabled(false);
    m_cancelButton->setEnabled(false);
    m_approvalNote->setText(
        QStringLiteral("Execution requires your explicit approval."));
    m_approvalNote->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; background: transparent; }")
        .arg(TP::TextMuted));
}

void TerminalPage::setBusy(bool generating)
{
    m_loadingBar->setVisible(generating);
    m_generateButton->setEnabled(!generating && !m_running);
    if (generating) {
        m_executeButton->setEnabled(false);
        m_cancelButton->setEnabled(false);
    }
}

void TerminalPage::setRunning(bool running)
{
    m_running = running;
    m_stopButton->setEnabled(running);
    m_generateButton->setEnabled(!running && !m_assistant->isBusy());
    m_executeButton->setEnabled(!running && m_hasProposal && m_proposal.executable);
    if (running) {
        m_explainButton->setEnabled(false);
    }
}

void TerminalPage::appendOutput(const QString &text, const QString &color)
{
    if (text.isEmpty()) {
        return;
    }
    m_outputView->moveCursor(QTextCursor::End);
    m_outputView->setTextColor(QColor(color));
    m_outputView->insertPlainText(text);
    m_outputView->moveCursor(QTextCursor::End);
}

void TerminalPage::appendRawOutput(const QString &text)
{
    m_capturedOutput += text;
    if (m_capturedOutput.size() > kMaxCapturedChars) {
        m_capturedOutput = m_capturedOutput.right(kMaxCapturedChars);
    }
}

void TerminalPage::updateStatus(const QString &text, const QString &color)
{
    m_inputStatus->setText(text);
    m_inputStatus->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; background: transparent; }").arg(color));
}

QString TerminalPage::riskBadge(const TerminalRisk &risk)
{
    switch (risk) {
    case TerminalRisk::Low:     return QStringLiteral("● LOW RISK");
    case TerminalRisk::Medium:  return QStringLiteral("● MEDIUM RISK");
    case TerminalRisk::High:    return QStringLiteral("● HIGH RISK");
    case TerminalRisk::Blocked: return QStringLiteral("● BLOCKED");
    }
    return QStringLiteral("● BLOCKED");
}

QString TerminalPage::riskColor(const TerminalRisk &risk)
{
    switch (risk) {
    case TerminalRisk::Low:     return TP::Success;
    case TerminalRisk::Medium:  return TP::Warning;
    case TerminalRisk::High:    return TP::Danger;
    case TerminalRisk::Blocked: return TP::Danger;
    }
    return TP::Danger;
}

void TerminalPage::onGenerateClicked()
{
    const QString request = m_requestEdit->toPlainText().trimmed();
    if (request.isEmpty()) {
        updateStatus(QStringLiteral("Describe the Linux task first."), TP::Warning);
        return;
    }

    m_currentRequest = request;
    clearProposal();
    updateStatus(QStringLiteral("Generating a command with the local model…"), TP::TextMuted);
    m_assistant->generateCommand(request);
}

void TerminalPage::onGenerationStarted(const QString &)
{
    setBusy(true);
    updateStatus(QStringLiteral("Generating a command with the local model…"), TP::TextMuted);
}

void TerminalPage::onProposalReady(const TerminalCommandProposal &proposal)
{
    setBusy(false);
    showProposal(proposal);
}

void TerminalPage::onGenerationFailed(const QString &error)
{
    setBusy(false);
    updateStatus(QStringLiteral("Generation failed: %1").arg(error), TP::Danger);
}

void TerminalPage::onExecuteClicked()
{
    if (!m_hasProposal || !m_proposal.executable || m_running) {
        return;
    }

    const QString display = m_proposal.displayCommand();
    const QString risk = terminalRiskName(m_proposal.risk).toUpper();

    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Approve command execution"));
    box.setIcon(QMessageBox::Warning);
    box.setText(QStringLiteral("Execute this command?"));
    box.setInformativeText(
        QStringLiteral("Command:\n%1\n\nRisk: %2\nWorking directory: %3\n\n"
                       "This runs locally with your user permissions. "
                       "TitanAI will not execute it automatically.")
            .arg(display,
                 risk,
                 m_proposal.workingDirectory.isEmpty() ? QStringLiteral("Home (default)")
                                                       : m_proposal.workingDirectory));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);
    box.button(QMessageBox::Yes)->setText(QStringLiteral("Execute"));

    if (box.exec() != QMessageBox::Yes) {
        updateStatus(QStringLiteral("Execution cancelled by the user."), TP::TextMuted);
        return;
    }

    m_lastCommand = display;
    m_lastExitCode = -1;
    m_lastDurationMs = 0;
    m_capturedOutput.clear();
    m_outputView->clear();

    TerminalHistoryEntry entry;
    entry.request     = m_currentRequest;
    entry.command     = display;
    entry.explanation = m_proposal.explanation;
    entry.risk        = m_proposal.risk;
    entry.timestamp   = QDateTime::currentDateTime();
    entry.status      = QStringLiteral("running");
    m_pendingHistoryId = m_history ? m_history->addEntry(entry) : QString();

    updateStatus(QStringLiteral("Executing approved command…"), TP::AccentGlow);

    if (!m_executor->execute(m_proposal, true)) {
        if (m_history && !m_pendingHistoryId.isEmpty()) {
            m_history->updateEntry(m_pendingHistoryId, QStringLiteral("rejected"));
        }
        m_pendingHistoryId.clear();
    }
}

void TerminalPage::onCancelClicked()
{
    if (m_running) {
        return;
    }
    clearProposal();
    m_currentRequest.clear();
    updateStatus(QStringLiteral("Command proposal cancelled."), TP::TextMuted);
}

void TerminalPage::onStopClicked()
{
    if (!m_running) {
        return;
    }
    updateStatus(QStringLiteral("Stopping execution…"), TP::Warning);
    m_executor->cancel();
}

void TerminalPage::onCopyCommandClicked()
{
    const QString command = m_hasProposal ? m_proposal.displayCommand()
                                          : m_commandLabel->text();
    QApplication::clipboard()->setText(command);
    updateStatus(QStringLiteral("Command copied to the clipboard."), TP::TextMuted);
}

void TerminalPage::onExplainOutputClicked()
{
    if (m_capturedOutput.trimmed().isEmpty()) {
        updateStatus(QStringLiteral("There is no output to explain yet."), TP::Warning);
        return;
    }
    m_expectingExplanation = true;
    m_assistant->explainOutput(
        m_currentRequest,
        m_lastCommand.isEmpty() ? m_proposal.displayCommand() : m_lastCommand,
        m_capturedOutput);
}

void TerminalPage::onExplanationStarted()
{
    if (!m_expectingExplanation) {
        return;
    }
    m_explainButton->setEnabled(false);
    updateStatus(QStringLiteral("Asking TitanAI to explain the output…"), TP::TextMuted);
}

void TerminalPage::onExplanationReady(const QString &explanation)
{
    if (!m_expectingExplanation) {
        return;
    }
    m_expectingExplanation = false;
    appendOutput(QStringLiteral("\n\n🤖 TitanAI explanation\n"), TP::AccentCyan);
    appendOutput(explanation + QStringLiteral("\n"), TP::TextPrimary);
    updateStatus(QStringLiteral("Explanation ready."), TP::Success);
    m_explainButton->setEnabled(!m_capturedOutput.trimmed().isEmpty());
}

void TerminalPage::onExplanationFailed(const QString &error)
{
    if (!m_expectingExplanation) {
        return;
    }
    m_expectingExplanation = false;
    updateStatus(QStringLiteral("Explanation failed: %1").arg(error), TP::Danger);
    m_explainButton->setEnabled(!m_capturedOutput.trimmed().isEmpty());
}

void TerminalPage::onExecutionStarted(const QString &displayCommand)
{
    m_outputView->clear();
    m_capturedOutput.clear();
    setRunning(true);
    updateStatus(QStringLiteral("Running approved command…"), TP::AccentGlow);
    m_outputStatus->setText(QStringLiteral("Running"));
    m_outputStatus->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; font-weight: 600; background: transparent; }")
        .arg(TP::AccentGlow));
    m_exitCodeLabel->setText(QStringLiteral("Exit code: —"));
    m_durationLabel->setText(QStringLiteral("Duration: —"));
    appendOutput(QStringLiteral("$ %1\n\n").arg(displayCommand), TP::AccentCyan);
}

void TerminalPage::onStandardOutput(const QString &chunk)
{
    appendRawOutput(chunk);
    appendOutput(chunk, TP::TextPrimary);
}

void TerminalPage::onStandardError(const QString &chunk)
{
    appendRawOutput(chunk);
    appendOutput(chunk, TP::Danger);
}

void TerminalPage::onOutputTruncated()
{
    appendOutput(QStringLiteral("\n[output truncated by TitanAI]\n"), TP::Warning);
}

void TerminalPage::onExecutionRejected(const QString &reason)
{
    updateStatus(QStringLiteral("Execution rejected: %1").arg(reason), TP::Danger);
    if (m_history && !m_pendingHistoryId.isEmpty()) {
        m_history->updateEntry(m_pendingHistoryId, QStringLiteral("rejected"));
        m_pendingHistoryId.clear();
    }
    setRunning(false);
}

void TerminalPage::onExecutionFailedToStart(const QString &error)
{
    updateStatus(QStringLiteral("Failed to start command: %1").arg(error), TP::Danger);
    m_outputStatus->setText(QStringLiteral("Failed to start"));
    m_outputStatus->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; font-weight: 600; background: transparent; }")
        .arg(TP::Danger));
    appendOutput(QStringLiteral("Error: %1\n").arg(error), TP::Danger);
    if (m_history && !m_pendingHistoryId.isEmpty()) {
        m_history->updateEntry(m_pendingHistoryId, QStringLiteral("failed"));
        m_pendingHistoryId.clear();
    }
    setRunning(false);
}

void TerminalPage::onExecutionFinished(int exitCode, QProcess::ExitStatus exitStatus,
                                       qint64 durationMs, bool timedOut, bool cancelled)
{
    m_lastExitCode = exitCode;
    m_lastDurationMs = durationMs;

    QString status;
    QString color;
    QString historyStatus;

    if (cancelled) {
        status = QStringLiteral("Cancelled");
        color = TP::Warning;
        historyStatus = QStringLiteral("cancelled");
    } else if (timedOut) {
        status = QStringLiteral("Timed out");
        color = TP::Warning;
        historyStatus = QStringLiteral("timeout");
    } else if (exitStatus == QProcess::CrashExit) {
        status = QStringLiteral("Crashed");
        color = TP::Danger;
        historyStatus = QStringLiteral("crashed");
    } else if (exitCode == 0) {
        status = QStringLiteral("Completed successfully");
        color = TP::Success;
        historyStatus = QStringLiteral("success");
    } else {
        status = QStringLiteral("Finished with errors");
        color = TP::Danger;
        historyStatus = QStringLiteral("failed");
    }

    m_outputStatus->setText(status);
    m_outputStatus->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; font-weight: 600; background: transparent; }")
        .arg(color));
    m_exitCodeLabel->setText(QStringLiteral("Exit code: %1").arg(exitCode));
    m_durationLabel->setText(QStringLiteral("Duration: %1 ms").arg(durationMs));

    appendOutput(QStringLiteral("\n\n[%1 — exit %2, %3 ms]\n").arg(status).arg(exitCode).arg(durationMs),
                 color);

    if (m_history && !m_pendingHistoryId.isEmpty()) {
        m_history->updateEntry(m_pendingHistoryId, historyStatus, exitCode, durationMs);
        m_pendingHistoryId.clear();
    }

    setRunning(false);
    m_explainButton->setEnabled(!m_capturedOutput.trimmed().isEmpty());
    updateStatus(status, color);
}

void TerminalPage::refreshHistory()
{
    if (!m_historyList || !m_history) {
        return;
    }

    const QString selectedId = m_historyList->currentItem()
                                   ? m_historyList->currentItem()->data(Qt::UserRole).toString()
                                   : QString();

    m_historyList->clear();

    const QList<TerminalHistoryEntry> entries = m_history->allEntries();
    for (const TerminalHistoryEntry &entry : entries) {
        QString text = QStringLiteral("%1  %2\n%3 — %4")
                           .arg(entry.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm")),
                                riskBadge(entry.risk),
                                entry.status,
                                entry.command);
        if (!entry.request.isEmpty()) {
            QString request = entry.request.simplified();
            if (request.size() > 70) {
                request = request.left(70) + QStringLiteral("…");
            }
            text += QStringLiteral("\n%1").arg(request);
        }

        auto *item = new QListWidgetItem(text, m_historyList);
        item->setData(Qt::UserRole, entry.id);
        item->setToolTip(entry.explanation);
        item->setForeground(QColor(riskColor(entry.risk)));

        if (entry.id == selectedId) {
            m_historyList->setCurrentItem(item);
        }
    }
}

void TerminalPage::onHistorySelectionChanged()
{
    const bool hasSelection = m_historyList->currentItem() != nullptr;
    m_historyCopyButton->setEnabled(hasSelection);
    m_historyRegenerateButton->setEnabled(hasSelection);
}

namespace {

const TerminalHistoryEntry *findEntry(const QList<TerminalHistoryEntry> &entries,
                                      const QString &id)
{
    for (const TerminalHistoryEntry &entry : entries) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

void TerminalPage::onHistoryCopyClicked()
{
    if (!m_historyList->currentItem() || !m_history) {
        return;
    }
    const QString id = m_historyList->currentItem()->data(Qt::UserRole).toString();
    const QList<TerminalHistoryEntry> entries = m_history->allEntries();
    const TerminalHistoryEntry *entry = findEntry(entries, id);
    if (entry) {
        QApplication::clipboard()->setText(entry->command);
        updateStatus(QStringLiteral("History command copied to the clipboard."), TP::TextMuted);
    }
}

void TerminalPage::onHistoryRegenerateClicked()
{
    if (!m_historyList->currentItem() || !m_history) {
        return;
    }
    const QString id = m_historyList->currentItem()->data(Qt::UserRole).toString();
    const QList<TerminalHistoryEntry> entries = m_history->allEntries();
    const TerminalHistoryEntry *entry = findEntry(entries, id);
    if (!entry) {
        return;
    }

    m_requestEdit->setPlainText(entry->request);
    m_currentRequest = entry->request;
    clearProposal();
    updateStatus(QStringLiteral("Regenerating command…"), TP::TextMuted);
    m_assistant->generateCommand(entry->request);
}

void TerminalPage::onClearHistoryClicked()
{
    if (!m_history || m_history->count() == 0) {
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        QStringLiteral("Clear terminal history"),
        QStringLiteral("Delete all locally stored terminal command history?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (answer == QMessageBox::Yes) {
        m_history->clear();
        updateStatus(QStringLiteral("Terminal history cleared."), TP::TextMuted);
    }
}
