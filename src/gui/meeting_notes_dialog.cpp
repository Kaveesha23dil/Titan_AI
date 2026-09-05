#include "gui/meeting_notes_dialog.hpp"
#include "agent/agent.hpp"
#include "gui/export_utils.hpp"

#include <QCloseEvent>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QStyle>
#include <QTextBrowser>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

// ─────────────────────────────────────────────────────────────────────────────
//  Small helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace {

QString formatClock(const QDateTime &dt, const QString &fallback = QStringLiteral("--:--:--"))
{
    return dt.isValid() ? dt.toString(QStringLiteral("hh:mm:ss")) : fallback;
}

QString formatElapsed(const QDateTime &started)
{
    if (!started.isValid()) {
        return QStringLiteral("00:00:00");
    }
    qint64 total = started.secsTo(QDateTime::currentDateTime());
    if (total < 0) {
        total = 0;
    }
    const qint64 h = total / 3600;
    const qint64 m = (total % 3600) / 60;
    const qint64 s = total % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  Construction
// ─────────────────────────────────────────────────────────────────────────────
MeetingNotesDialog::MeetingNotesDialog(Agent *agent, QWidget *parent)
    : QDialog(parent)
    , m_agent(agent)
{
    setWindowTitle(QStringLiteral("Meeting Notes"));
    setMinimumSize(760, 560);
    resize(920, 680);
    setAttribute(Qt::WA_DeleteOnClose);

    setupUi();
    setupStylesheet();

    // Use the current chat model for summarization so results feel consistent.
    if (m_agent) {
        m_recorder.setModel(m_agent->currentModel());
    }

    connect(&m_recorder, &MeetingRecorder::recordingChanged, this,
            [this](bool recording) { setRecordingUi(recording); });
    connect(&m_recorder, &MeetingRecorder::entryAdded, this, &MeetingNotesDialog::onEntryAdded);
    connect(&m_recorder, &MeetingRecorder::micLevelChanged, this, &MeetingNotesDialog::onMicLevel);
    connect(&m_recorder, &MeetingRecorder::summaryReady, this, &MeetingNotesDialog::onSummaryReady);
    connect(&m_recorder, &MeetingRecorder::summaryError, this, &MeetingNotesDialog::onSummaryError);
    connect(&m_recorder, &MeetingRecorder::statusChanged, this, &MeetingNotesDialog::onStatus);
    connect(&m_recorder, &MeetingRecorder::errorOccurred, this, &MeetingNotesDialog::onError);
    connect(&m_recorder, &MeetingRecorder::partialTranscript, this,
            [this](const QString &text) {
                if (!text.trimmed().isEmpty()) {
                    m_statusLabel->setText(QStringLiteral("… %1").arg(text.simplified()));
                }
            });
    connect(m_noteEdit, &QLineEdit::returnPressed, this, &MeetingNotesDialog::onAddNote);

    QShortcut *esc = new QShortcut(QKeySequence::Cancel, this);
    connect(esc, &QShortcut::activated, this, &QDialog::close);

    refreshView();
}

MeetingNotesDialog::~MeetingNotesDialog()
{
    if (m_recorder.isRecording()) {
        m_recorder.stopRecording();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  UI Setup
// ─────────────────────────────────────────────────────────────────────────────
void MeetingNotesDialog::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Header ───────────────────────────────────────────────────────────────
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("meetingHeader"));
    header->setFixedHeight(96);
    auto *headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(16, 10, 16, 8);
    headerLayout->setSpacing(8);

    // Row 1: title, record, summary, clear, close
    auto *row1 = new QHBoxLayout;
    row1->setSpacing(8);

    m_titleEdit = new QLineEdit(header);
    m_titleEdit->setObjectName(QStringLiteral("titleEdit"));
    m_titleEdit->setPlaceholderText(QStringLiteral("Meeting title…"));
    m_titleEdit->setText(QStringLiteral("Meeting Notes"));
    row1->addWidget(m_titleEdit, 1);

    m_recordBtn = new QPushButton(QStringLiteral("●  Record Meeting"), header);
    m_recordBtn->setObjectName(QStringLiteral("recordBtn"));
    row1->addWidget(m_recordBtn);

    m_summaryBtn = new QPushButton(QStringLiteral("✦  Generate Summary"), header);
    m_summaryBtn->setObjectName(QStringLiteral("summaryBtn"));
    row1->addWidget(m_summaryBtn);

    m_clearBtn = new QPushButton(QStringLiteral("🗑 Clear"), header);
    m_clearBtn->setObjectName(QStringLiteral("clearBtn"));
    row1->addWidget(m_clearBtn);

    auto *closeBtn = new QPushButton(QStringLiteral("✕"), header);
    closeBtn->setObjectName(QStringLiteral("closeBtn"));
    closeBtn->setFixedSize(34, 34);
    row1->addWidget(closeBtn);

    headerLayout->addLayout(row1);

    // Row 2: status dot + elapsed, mic level, entry count
    auto *row2 = new QHBoxLayout;
    row2->setSpacing(12);

    m_elapsedLabel = new QLabel(QStringLiteral("○ Idle"), header);
    m_elapsedLabel->setObjectName(QStringLiteral("elapsedLabel"));
    row2->addWidget(m_elapsedLabel);

    m_micBar = new QProgressBar(header);
    m_micBar->setObjectName(QStringLiteral("micBar"));
    m_micBar->setRange(0, 100);
    m_micBar->setValue(0);
    m_micBar->setTextVisible(false);
    m_micBar->setFixedWidth(120);
    row2->addWidget(m_micBar);

    m_entryCountLabel = new QLabel(QStringLiteral("0 entries"), header);
    m_entryCountLabel->setObjectName(QStringLiteral("entryCount"));
    row2->addWidget(m_entryCountLabel);

    row2->addStretch(1);

    headerLayout->addLayout(row2);

    root->addWidget(header);

    auto *sep = new QWidget(this);
    sep->setFixedHeight(1);
    sep->setObjectName(QStringLiteral("separator"));
    root->addWidget(sep);

    // ── Transcript preview ────────────────────────────────────────────────────
    m_browser = new QTextBrowser(this);
    m_browser->setObjectName(QStringLiteral("notesBrowser"));
    m_browser->setReadOnly(true);
    m_browser->setOpenExternalLinks(false);
    m_browser->setFrameShape(QFrame::NoFrame);
    m_browser->document()->setDefaultStyleSheet(QStringLiteral(
        "body { margin: 0; padding: 12px 16px; background: #0b0f19; color: #f1f5f9; }"
        ".title { color: #f1f5f9; font-size: 18px; font-weight: 700; }"
        ".subtitle { color: #64748b; font-size: 12px; margin: 2px 0 14px 0; }"
        "h2 { color: #818cf8; font-size: 14px; font-weight: 700; margin: 18px 0 8px 0; }"
        "p { font-size: 13px; line-height: 1.55; margin: 6px 0; }"
        "ul { font-size: 13px; line-height: 1.5; margin: 6px 0; padding-left: 22px; }"
        ".summary-box { background: #151d2e; border: 1px solid #1e293b; border-radius: 8px;"
        "                padding: 4px 14px; margin-bottom: 8px; }"
        ".msg { margin: 8px 0; padding: 10px 14px; border-radius: 6px; }"
        ".msg-speech { background: #131a2e; border-left: 3px solid #6366f1; }"
        ".msg-note { background: #1c1a12; border-left: 3px solid #eab308; }"
        ".sender { font-size: 11px; font-weight: 700; color: #94a3b8; margin-bottom: 3px; }"
        ".content { font-size: 13px; color: #f1f5f9; white-space: pre-wrap; }"
        ".empty { color: #64748b; text-align: center; font-size: 14px; padding: 60px 0; }"
    ));
    root->addWidget(m_browser, 1);

    // ── Bottom bar: note input + actions ─────────────────────────────────────
    auto *footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("footer"));
    footer->setFixedHeight(118);
    auto *footerLayout = new QVBoxLayout(footer);
    footerLayout->setContentsMargins(14, 8, 14, 10);
    footerLayout->setSpacing(6);

    m_statusLabel = new QLabel(QStringLiteral("Ready."), footer);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    footerLayout->addWidget(m_statusLabel);

    auto *noteRow = new QHBoxLayout;
    noteRow->setSpacing(8);

    m_noteEdit = new QLineEdit(footer);
    m_noteEdit->setObjectName(QStringLiteral("noteEdit"));
    m_noteEdit->setPlaceholderText(QStringLiteral("Type a manual note (works without microphone)…"));
    noteRow->addWidget(m_noteEdit, 1);

    auto *addBtn = new QPushButton(QStringLiteral("➕ Add Note"), footer);
    addBtn->setObjectName(QStringLiteral("addNoteBtn"));
    noteRow->addWidget(addBtn);

    footerLayout->addLayout(noteRow);

    auto *actionRow = new QHBoxLayout;
    actionRow->setSpacing(8);

    auto *saveMdBtn = new QPushButton(QStringLiteral("⬇  Markdown"), footer);
    saveMdBtn->setObjectName(QStringLiteral("saveBtn"));
    auto *savePdfBtn = new QPushButton(QStringLiteral("⬇  PDF"), footer);
    savePdfBtn->setObjectName(QStringLiteral("saveBtn"));

    m_sendToChatBtn = new QPushButton(QStringLiteral("↗  Send Summary to Chat"), footer);
    m_sendToChatBtn->setObjectName(QStringLiteral("sendToChatBtn"));
    m_sendToChatBtn->setEnabled(false);

    actionRow->addWidget(saveMdBtn);
    actionRow->addWidget(savePdfBtn);
    actionRow->addWidget(m_sendToChatBtn);
    actionRow->addStretch(1);

    footerLayout->addLayout(actionRow);

    root->addWidget(footer);

    // ── Connections ───────────────────────────────────────────────────────────
    connect(m_recordBtn, &QPushButton::clicked, this, &MeetingNotesDialog::onRecordClicked);
    connect(m_summaryBtn, &QPushButton::clicked, this, &MeetingNotesDialog::onGenerateSummary);
    connect(m_clearBtn, &QPushButton::clicked, this, &MeetingNotesDialog::onClear);
    connect(addBtn, &QPushButton::clicked, this, &MeetingNotesDialog::onAddNote);
    connect(saveMdBtn, &QPushButton::clicked, this, &MeetingNotesDialog::exportMarkdown);
    connect(savePdfBtn, &QPushButton::clicked, this, &MeetingNotesDialog::exportPdf);
    connect(m_sendToChatBtn, &QPushButton::clicked, this, &MeetingNotesDialog::sendSummaryToChat);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);

    m_timer = new QTimer(this);
    m_timer->setInterval(500);
    connect(m_timer, &QTimer::timeout, this, &MeetingNotesDialog::updateElapsed);
}

void MeetingNotesDialog::setupStylesheet()
{
    setStyleSheet(QStringLiteral(R"(
        MeetingNotesDialog { background: #0b0f19; }
        #meetingHeader { background: #111827; border-bottom: 1px solid #1e293b; }
        #separator { background: #1e293b; }
        #notesBrowser { background: #0b0f19; border: none; color: #f1f5f9; }
        #footer { background: #111827; border-top: 1px solid #1e293b; }
        #statusLabel { color: #94a3b8; font-size: 12px; }
        #elapsedLabel { color: #94a3b8; font-size: 12px; font-weight: 600; }
        #entryCount { color: #6366f1; font-size: 12px; font-weight: 600; }
        #titleEdit {
            background: #0f1629; color: #f1f5f9; border: 1px solid #1e293b;
            border-radius: 8px; padding: 8px 14px; font-size: 13px;
        }
        #titleEdit:focus { border-color: #6366f1; }
        #noteEdit {
            background: #0f1629; color: #f1f5f9; border: 1px solid #1e293b;
            border-radius: 8px; padding: 9px 14px; font-size: 13px;
        }
        #noteEdit:focus { border-color: #6366f1; }
        #micBar {
            background: #0f1629; border: 1px solid #1e293b; border-radius: 4px;
            min-height: 8px; max-height: 8px;
        }
        #micBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #6366f1,stop:1 #22d3ee); border-radius: 3px; }
        #recordBtn {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #ef4444,stop:1 #f97316);
            color: white; border: none; border-radius: 8px; padding: 9px 16px;
            font-size: 13px; font-weight: 600;
        }
        #recordBtn:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #f87171,stop:1 #fb923c); }
        #recordBtn[recording="true"] { background: #1e293b; color: #ef4444; border: 1px solid #ef4444; }
        #summaryBtn {
            background: #6366f1; color: white; border: none; border-radius: 8px;
            padding: 9px 16px; font-size: 13px; font-weight: 600;
        }
        #summaryBtn:hover { background: #818cf8; }
        #summaryBtn:disabled { background: #1e293b; color: #64748b; }
        #clearBtn, #saveBtn {
            background: #111827; color: #94a3b8; border: 1px solid #1e293b;
            border-radius: 8px; padding: 9px 14px; font-size: 13px;
        }
        #clearBtn:hover, #saveBtn:hover { background: #1a2236; color: #f1f5f9; border-color: #6366f1; }
        #sendToChatBtn {
            background: #0f1629; color: #22d3ee; border: 1px solid #22d3ee;
            border-radius: 8px; padding: 9px 14px; font-size: 13px;
        }
        #sendToChatBtn:hover { background: #15233a; }
        #sendToChatBtn:disabled { color: #475569; border-color: #1e293b; background: #0f1629; }
        #addNoteBtn { background: #22d3ee; color: #0f172a; border: none; border-radius: 8px; padding: 9px 16px; font-size: 13px; font-weight: 600; }
        #addNoteBtn:hover { background: #38bdf8; }
        #closeBtn {
            background: transparent; color: #64748b; border: 1px solid #1e293b;
            border-radius: 6px; font-size: 14px;
        }
        #closeBtn:hover { background: #2a1215; color: #ef4444; border-color: #ef4444; }
        QScrollBar:vertical { background: #0b0f19; width: 6px; border: none; }
        QScrollBar::handle:vertical { background: #1e293b; border-radius: 3px; }
        QScrollBar::handle:vertical:hover { background: #6366f1; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
    )"));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Slots
// ─────────────────────────────────────────────────────────────────────────────
void MeetingNotesDialog::onRecordClicked()
{
    if (m_recorder.isRecording()) {
        m_recorder.stopRecording();
    } else {
        m_recorder.startRecording();
    }
}

void MeetingNotesDialog::onAddNote()
{
    const QString text = m_noteEdit->text();
    if (text.trimmed().isEmpty()) {
        return;
    }
    m_recorder.addNote(text);
    m_noteEdit->clear();
    m_noteEdit->setFocus();
}

void MeetingNotesDialog::onGenerateSummary()
{
    m_summaryBtn->setEnabled(false);
    m_recorder.requestSummary();
}

void MeetingNotesDialog::onClear()
{
    if (m_recorder.isRecording()) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Clear Meeting"),
            QStringLiteral("Recording is active. Stop recording and clear all entries?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
        m_recorder.stopRecording();
    }
    m_recorder.clear();
    m_summary.clear();
    m_sendToChatBtn->setEnabled(false);
    refreshView();
}

void MeetingNotesDialog::exportMarkdown()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export Meeting Notes as Markdown"),
        effectiveTitle() + QStringLiteral(".md"),
        QStringLiteral("Markdown Files (*.md)"));
    if (path.isEmpty()) {
        return;
    }

    const QString md = MeetingRecorder::formatNotesMarkdown(
        effectiveTitle(),
        m_recorder.startedTime(),
        m_recorder.endedTime(),
        m_recorder.entries(),
        m_summary);

    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&f);
        out << md;
        m_statusLabel->setText(QStringLiteral("Saved Markdown: %1").arg(path));
    }
}

void MeetingNotesDialog::exportPdf()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export Meeting Notes as PDF"),
        effectiveTitle() + QStringLiteral(".pdf"),
        QStringLiteral("PDF Files (*.pdf)"));
    if (path.isEmpty()) {
        return;
    }

    const QString html = MeetingRecorder::formatNotesHtml(
        effectiveTitle(),
        m_recorder.startedTime(),
        m_recorder.endedTime(),
        m_recorder.entries(),
        m_summary);

    QString error;
    if (ExportUtils::writeHtmlToPdf(html, effectiveTitle(), path, &error)) {
        m_statusLabel->setText(QStringLiteral("Saved PDF: %1").arg(path));
    } else {
        QMessageBox::warning(this, QStringLiteral("Export Failed"),
                             QStringLiteral("Could not save the meeting notes as a PDF.\n%1").arg(error));
    }
}

void MeetingNotesDialog::sendSummaryToChat()
{
    if (m_summary.trimmed().isEmpty()) {
        return;
    }
    emit sendToChatRequested(m_summary);
    m_sendToChatBtn->setEnabled(false);
    m_statusLabel->setText(QStringLiteral("Summary sent to the chat."));
}

void MeetingNotesDialog::onEntryAdded()
{
    refreshView();
    m_sendToChatBtn->setEnabled(!m_summary.trimmed().isEmpty());
}

void MeetingNotesDialog::onMicLevel(float level)
{
    m_micBar->setValue(qBound(0, static_cast<int>(level * 100.0f), 100));
}

void MeetingNotesDialog::onSummaryReady(const QString &markdown)
{
    m_summary = markdown;
    m_summaryBtn->setEnabled(true);
    m_sendToChatBtn->setEnabled(true);
    refreshView();
}

void MeetingNotesDialog::onSummaryError(const QString &error)
{
    m_summaryBtn->setEnabled(true);
    m_statusLabel->setText(error);
}

void MeetingNotesDialog::onStatus(const QString &message)
{
    m_statusLabel->setText(message);
}

void MeetingNotesDialog::onError(const QString &message)
{
    m_statusLabel->setText(message);
    if (m_recorder.isRecording()) {
        m_recorder.stopRecording();
    }
}

void MeetingNotesDialog::updateElapsed()
{
    if (m_recorder.isRecording()) {
        m_elapsedLabel->setText(QStringLiteral("● Recording · %1")
                                    .arg(formatElapsed(m_recorder.startedTime())));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  UI helpers
// ─────────────────────────────────────────────────────────────────────────────
void MeetingNotesDialog::setRecordingUi(bool recording)
{
    if (recording) {
        m_recordBtn->setText(QStringLiteral("■  Stop Meeting"));
        m_recordBtn->setProperty("recording", true);
        m_recordBtn->style()->unpolish(m_recordBtn);
        m_recordBtn->style()->polish(m_recordBtn);
        m_elapsedLabel->setText(QStringLiteral("● Recording · %1")
                                    .arg(formatElapsed(m_recorder.startedTime())));
        m_timer->start();
    } else {
        m_recordBtn->setText(QStringLiteral("●  Record Meeting"));
        m_recordBtn->setProperty("recording", false);
        m_recordBtn->style()->unpolish(m_recordBtn);
        m_recordBtn->style()->polish(m_recordBtn);
        m_timer->stop();
        m_elapsedLabel->setText(QStringLiteral("○ Stopped"));
    }
    refreshView();
}

void MeetingNotesDialog::refreshView()
{
    m_browser->setHtml(buildHtml());
    m_entryCountLabel->setText(QStringLiteral("%1 entry(ies)").arg(m_recorder.entries().size()));
    m_summaryBtn->setEnabled(!m_recorder.entries().isEmpty());
    m_sendToChatBtn->setEnabled(!m_summary.trimmed().isEmpty());
}

QString MeetingNotesDialog::effectiveTitle() const
{
    const QString t = m_titleEdit->text().trimmed();
    return t.isEmpty() ? QStringLiteral("Meeting Notes") : t;
}

QString MeetingNotesDialog::buildHtml() const
{
    const QList<MeetingEntry> entries = m_recorder.entries();

    QString html;
    html += QStringLiteral("<div class='title'>%1</div>")
                .arg(effectiveTitle().toHtmlEscaped());
    const QString started = m_recorder.startedTime().isValid()
                                ? m_recorder.startedTime().toString(QStringLiteral("yyyy-MM-dd hh:mm"))
                                : QStringLiteral("—");
    html += QStringLiteral("<div class='subtitle'>Started %1 · %2 entry(ies) · "
                           "summary ready: %3</div>")
                .arg(started.toHtmlEscaped())
                .arg(entries.size())
                .arg(m_summary.trimmed().isEmpty() ? QStringLiteral("no")
                                                   : QStringLiteral("yes"));

    if (!m_summary.trimmed().isEmpty()) {
        html += QStringLiteral("<h2>Summary</h2><div class='summary-box'>%1</div>")
                    .arg(MeetingRecorder::renderMarkdownToHtml(m_summary));
    }

    html += QStringLiteral("<h2>Transcript</h2>");
    if (entries.isEmpty()) {
        html += QStringLiteral(
            "<div class='empty'>No entries yet. Press Record Meeting to capture "
            "live transcription, or type a note below.</div>");
    } else {
        for (const MeetingEntry &entry : entries) {
            const QString css = entry.manual ? QStringLiteral("msg-note")
                                             : QStringLiteral("msg-speech");
            const QString label = entry.manual ? QStringLiteral("📝 Note")
                                               : QStringLiteral("🎙 Speech · %1")
                                                     .arg(formatClock(entry.time));
            html += QStringLiteral("<div class='msg %1'><div class='sender'>%2</div>"
                                   "<div class='content'>%3</div></div>")
                        .arg(css, label, entry.text.toHtmlEscaped());
        }
    }

    return QStringLiteral("<html><body>%1</body></html>").arg(html);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Event handling
// ─────────────────────────────────────────────────────────────────────────────
void MeetingNotesDialog::closeEvent(QCloseEvent *event)
{
    if (m_recorder.isRecording()) {
        m_recorder.stopRecording();
    }
    QDialog::closeEvent(event);
}