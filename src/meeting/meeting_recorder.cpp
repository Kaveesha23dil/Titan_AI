#include "meeting/meeting_recorder.hpp"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>

// ─────────────────────────────────────────────────────────────────────────────
//  Static helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace {

QString formatTime(const QDateTime &dt, const QString &fallback = QStringLiteral("—"))
{
    return dt.isValid() ? dt.toString(QStringLiteral("yyyy-MM-dd hh:mm")) : fallback;
}

QString formatDuration(const QDateTime &started, const QDateTime &ended)
{
    if (!started.isValid()) {
        return QStringLiteral("—");
    }
    QDateTime end = ended.isValid() ? ended : QDateTime::currentDateTime();
    if (end < started) {
        return QStringLiteral("—");
    }
    const qint64 totalSecs = static_cast<qint64>(started.secsTo(end));
    const qint64 hours = totalSecs / 3600;
    const qint64 mins  = (totalSecs % 3600) / 60;
    const qint64 secs  = totalSecs % 60;

    if (hours > 0) {
        return QStringLiteral("%1h %2m %3s").arg(hours).arg(mins).arg(secs);
    }
    if (mins > 0) {
        return QStringLiteral("%1m %2s").arg(mins).arg(secs);
    }
    return QStringLiteral("%1s").arg(secs);
}

QString entryClock(const QDateTime &dt)
{
    return dt.isValid() ? dt.toString(QStringLiteral("hh:mm")) : QStringLiteral("--:--");
}

QString entryMarkdownLine(const MeetingEntry &entry)
{
    const QString tag = entry.manual ? QStringLiteral("note") : QStringLiteral("speech");
    return QStringLiteral("- **%1 · %2** — %3")
               .arg(entryClock(entry.time), tag, entry.text.trimmed());
}

QString entryHtml(const MeetingEntry &entry)
{
    const QString cssClass = entry.manual ? QStringLiteral("msg-note")
                                          : QStringLiteral("msg-speech");
    const QString label = entry.manual ? QStringLiteral("📝 Note")
                                       : QStringLiteral("🎙 Speech");
    return QStringLiteral(
               "<div class='msg %1'>"
               "<div class='sender'>%2 <span class='ts'>· %3</span></div>"
               "<div class='content'>%4</div>"
               "</div>")
        .arg(cssClass, label, entryClock(entry.time), entry.text.toHtmlEscaped());
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  MeetingRecorder
// ─────────────────────────────────────────────────────────────────────────────

MeetingRecorder::MeetingRecorder(QObject *parent)
    : QObject(parent)
    , m_capture(this)
    , m_recognizer(this)
    , m_ollama(this)
{
    connect(&m_capture, &AudioCapture::audioChunk, this, &MeetingRecorder::onAudioChunk);
    connect(&m_capture, &AudioCapture::levelChanged, this, &MeetingRecorder::micLevelChanged);
    connect(&m_capture, &AudioCapture::captureError, this, &MeetingRecorder::errorOccurred);

    connect(&m_recognizer, &SpeechRecognizer::partialTranscript,
            this, &MeetingRecorder::partialTranscript);
    connect(&m_recognizer, &SpeechRecognizer::finalTranscript,
            this, &MeetingRecorder::onFinalText);
    connect(&m_recognizer, &SpeechRecognizer::errorOccurred,
            this, &MeetingRecorder::errorOccurred);

    connect(&m_ollama, &OllamaClient::completionReceived,
            this, &MeetingRecorder::onSummaryReceived);
    connect(&m_ollama, &OllamaClient::completionError,
            this, &MeetingRecorder::summaryError);
}

MeetingRecorder::~MeetingRecorder()
{
    if (m_recording) {
        stopRecording();
    }
}

bool MeetingRecorder::sttAvailable()
{
    return SpeechRecognizer::isAvailable();
}

bool MeetingRecorder::isRecording() const
{
    return m_recording;
}

QDateTime MeetingRecorder::startedTime() const
{
    return m_started;
}

QDateTime MeetingRecorder::endedTime() const
{
    return m_ended;
}

bool MeetingRecorder::startRecording()
{
    if (m_recording) {
        return true;
    }

    if (!SpeechRecognizer::isAvailable()) {
        emit errorOccurred(QStringLiteral(
            "Speech recognition (Vosk) is not installed, so meeting audio cannot be "
            "transcribed. Install 'vosk-api' and a Vosk model to enable live "
            "transcription. You can still take notes and summarize text manually."));
        return false;
    }

    m_capture.start(); // sets the sample rate; emits captureError on failure

    if (!ensureRecognizerStarted()) {
        m_capture.stop();
        return false;
    }

    m_recognizer.reset();
    m_recording = true;
    m_started = QDateTime::currentDateTime();
    m_ended = QDateTime();
    m_elapsed.restart();

    emit recordingChanged(true);
    emit statusChanged(QStringLiteral("Recording meeting… (microphone live transcription)"));
    return true;
}

void MeetingRecorder::stopRecording()
{
    if (!m_recording) {
        return;
    }

    // Flush any remaining audio through the recognizer so the last utterance is
    // finalized before we close the capture stream. Entries emitted here are
    // still appended because the recorder simply consumes finalText signals.
    m_recognizer.flush();
    m_capture.stop();

    m_recording = false;
    m_ended = QDateTime::currentDateTime();

    emit recordingChanged(false);
    emit statusChanged(QStringLiteral("Recording stopped. %1 entry(ies) captured.")
                           .arg(m_entries.size()));
}

void MeetingRecorder::addNote(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    MeetingEntry entry;
    entry.time   = QDateTime::currentDateTime();
    entry.text   = trimmed;
    entry.manual = true;
    m_entries.append(entry);

    emit entryAdded(entry);
    emit statusChanged(QStringLiteral("Note added."));
}

void MeetingRecorder::clear()
{
    m_entries.clear();
    m_ended = QDateTime();
    emit statusChanged(QStringLiteral("Meeting notes cleared."));
}

void MeetingRecorder::setModel(const QString &model)
{
    if (!model.trimmed().isEmpty()) {
        m_model = model.trimmed();
    }
}

void MeetingRecorder::requestSummary()
{
    if (m_summarizing) {
        return;
    }

    const QString transcript = transcriptText();
    if (transcript.trimmed().isEmpty()) {
        emit summaryError(QStringLiteral(
            "Nothing to summarize yet. Record some speech or add notes first."));
        return;
    }

    m_summarizing = true;
    m_ollama.setModel(m_model);
    m_ollama.requestCompletion(buildSummaryPrompt(transcript));

    emit statusChanged(QStringLiteral("Generating meeting summary…"));
}

bool MeetingRecorder::isSummarizing() const
{
    return m_summarizing;
}

QList<MeetingEntry> MeetingRecorder::entries() const
{
    return m_entries;
}

QString MeetingRecorder::transcriptText() const
{
    QStringList lines;
    for (const MeetingEntry &entry : m_entries) {
        lines << entry.text.trimmed();
    }
    return lines.join(QLatin1Char('\n'));
}

QString MeetingRecorder::transcriptMarkdown() const
{
    QStringList lines;
    for (const MeetingEntry &entry : m_entries) {
        lines << entryMarkdownLine(entry);
    }
    return lines.join(QLatin1Char('\n'));
}

QString MeetingRecorder::buildSummaryPrompt(const QString &transcript)
{
    return QStringLiteral(
               "You are an expert meeting assistant. Summarize the meeting transcript below "
               "into clean, well-structured Markdown notes. Be concise but complete.\n\n"
               "Use exactly these sections:\n"
               "## Suggested Title\n"
               "A short title for this meeting.\n\n"
               "## Key Points\n"
               "- Bullet list of the main topics and important details.\n\n"
               "## Decisions\n"
               "- Decisions reached (or 'No explicit decisions recorded.').\n\n"
               "## Action Items\n"
               "- Each item on its own bullet. If none, write 'No action items recorded.'\n\n"
               "## Open Questions\n"
               "- Any unanswered questions. If none, write 'No open questions.'\n\n"
               "Do not mention that you made a summary; just output the Markdown.\n\n"
               "---\n\nMeeting transcript:\n\n%1\n")
        .arg(transcript);
}

QString MeetingRecorder::formatNotesMarkdown(const QString &title,
                                             const QDateTime &started,
                                             const QDateTime &ended,
                                             const QList<MeetingEntry> &entries,
                                             const QString &summary)
{
    QString md;
    md += QStringLiteral("# %1\n\n").arg(title.trimmed().isEmpty()
                                             ? QStringLiteral("Meeting Notes")
                                             : title.trimmed());
    md += QStringLiteral("*Started: %1 | Ended: %2 | Duration: %3 | Entries: %4*\n\n")
              .arg(formatTime(started), formatTime(ended),
                   formatDuration(started, ended))
              .arg(entries.size());
    md += QStringLiteral("---\n\n");

    md += QStringLiteral("## Summary\n\n");
    md += summary.trimmed().isEmpty()
              ? QStringLiteral("_No summary generated yet._\n\n")
              : summary.trimmed() + QStringLiteral("\n\n");
    md += QStringLiteral("---\n\n");

    md += QStringLiteral("## Transcript\n\n");
    if (entries.isEmpty()) {
        md += QStringLiteral("_No entries recorded._\n");
    } else {
        QStringList lines;
        for (const MeetingEntry &entry : entries) {
            lines << entryMarkdownLine(entry);
        }
        md += lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    }

    return md;
}

QString MeetingRecorder::renderMarkdownToHtml(const QString &markdown)
{
    if (markdown.trimmed().isEmpty()) {
        return {};
    }

    static const QRegularExpression headingRe(QStringLiteral("^(#{1,3})\\s+(.+)$"));
    static const QRegularExpression bulletRe(QStringLiteral("^[-*•]\\s+(.+)$"));
    static const QRegularExpression boldRe(QStringLiteral("\\*\\*(.+?)\\*\\*"));

    auto decorate = [](const QString &escaped) {
        return QString(escaped).replace(boldRe, QStringLiteral("<b>\\1</b>"));
    };

    const QStringList rawLines = markdown.split(QLatin1Char('\n'));

    QStringList html;
    bool inList = false;
    auto closeList = [&html, &inList]() {
        if (inList) {
            html << QStringLiteral("</ul>");
            inList = false;
        }
    };

    for (const QString &rawLine : rawLines) {
        const QString line = rawLine.toHtmlEscaped();

        const QRegularExpressionMatch heading = headingRe.match(line);
        if (heading.hasMatch()) {
            closeList();
            const QString level = heading.captured(1).size() >= 3
                                      ? QStringLiteral("h3")
                                      : QStringLiteral("h2");
            html << QStringLiteral("<%1>%2</%1>").arg(level, decorate(heading.captured(2)));
            continue;
        }

        const QRegularExpressionMatch bullet = bulletRe.match(line);
        if (bullet.hasMatch()) {
            if (!inList) {
                html << QStringLiteral("<ul>");
                inList = true;
            }
            html << QStringLiteral("<li>%1</li>").arg(decorate(bullet.captured(1)));
            continue;
        }

        if (line.simplified().isEmpty()) {
            closeList();
            continue;
        }

        closeList();
        html << QStringLiteral("<p>%1</p>").arg(decorate(line));
    }
    closeList();

    return html.join(QLatin1Char('\n'));
}

QString MeetingRecorder::formatNotesHtml(const QString &title,
                                         const QDateTime &started,
                                         const QDateTime &ended,
                                         const QList<MeetingEntry> &entries,
                                         const QString &summary)
{
    static const QString kCss = QStringLiteral(R"(
        body { font-family: 'DejaVu Sans','Inter','Segoe UI',sans-serif; color:#1f2937;
               margin:0; padding:24px; background:#ffffff; }
        h1 { font-size:22px; color:#111827; margin:0 0 4px 0; }
        h2 { font-size:16px; color:#4f46e5; margin:18px 0 6px 0; }
        .meta { font-size:12px; color:#6b7280; margin:0 0 18px 0; }
        .summary { font-size:13px; line-height:1.55; background:#f8fafc;
                   border:1px solid #e5e7eb; border-radius:6px; padding:12px 14px; }
        .msg { margin:10px 0; padding:12px 14px; border-radius:6px;
               border:1px solid #e5e7eb; page-break-inside:avoid; }
        .msg-speech { background:#f5f3ff; border-left:4px solid #6366f1; }
        .msg-note   { background:#fefce8; border-left:4px solid #eab308; }
        .sender { font-size:11px; font-weight:700; color:#374151; margin-bottom:4px; }
        .ts { font-weight:400; color:#9ca3af; }
        .content { font-size:13px; line-height:1.55; word-break:break-word;
                   white-space:pre-wrap; }
        .empty { color:#9ca3af; text-align:center; font-size:14px; padding:40px; }
    )");

    const QString safeTitle = title.trimmed().isEmpty()
                                  ? QStringLiteral("Meeting Notes")
                                  : title.trimmed();

    QString html;
    html += QStringLiteral("<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n");
    html += QStringLiteral("<title>%1</title>\n").arg(safeTitle.toHtmlEscaped());
    html += QStringLiteral("<style>%1</style>\n</head>\n<body>\n").arg(kCss);

    html += QStringLiteral("<h1>%1</h1>\n").arg(safeTitle.toHtmlEscaped());
    html += QStringLiteral("<p class='meta'>Started %1 &nbsp;·&nbsp; Ended %2 "
                           "&nbsp;·&nbsp; Duration %3 &nbsp;·&nbsp; %4 entries</p>\n")
                .arg(formatTime(started), formatTime(ended),
                     formatDuration(started, ended))
                .arg(entries.size());

    html += QStringLiteral("<h2>Summary</h2>\n");
    if (summary.trimmed().isEmpty()) {
        html += QStringLiteral("<div class='summary'><em>No summary generated yet.</em></div>\n");
    } else {
        html += QStringLiteral("<div class='summary'>\n%1\n</div>\n")
                    .arg(renderMarkdownToHtml(summary));
    }

    html += QStringLiteral("<h2>Transcript</h2>\n");
    if (entries.isEmpty()) {
        html += QStringLiteral("<div class='empty'>No entries recorded.</div>\n");
    } else {
        for (const MeetingEntry &entry : entries) {
            html += entryHtml(entry);
        }
    }

    html += QStringLiteral("</body>\n</html>\n");
    return html;
}

// ── Private ──────────────────────────────────────────────────────────────────

bool MeetingRecorder::ensureRecognizerStarted()
{
    const int rate = m_capture.sampleRate() > 0 ? m_capture.sampleRate() : 16000;

    QString modelPath = resolveDefaultModelPath();
    if (modelPath.isEmpty()) {
        emit errorOccurred(QStringLiteral(
            "No Vosk speech recognition model found. Download one and set its path "
            "in Voice Settings, or take notes manually."));
        return false;
    }

    m_recognizer.startEngine(modelPath, rate);
    return true;
}

QString MeetingRecorder::resolveDefaultModelPath() const
{
    const QStringList candidates = {
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
            QStringLiteral("/vosk-model-small-en-us-0.15"),
        QStringLiteral("%1/.local/share/vosk-models/vosk-model-small-en-us-0.15")
            .arg(QDir::homePath()),
        QStringLiteral("/usr/share/vosk/vosk-model-small-en-us-0.15"),
        QStringLiteral("/usr/share/vosk/models/vosk-model-small-en-us-0.15"),
        QStringLiteral("/usr/share/vosk-models/vosk-model-small-en-us-0.15"),
    };

    for (const QString &candidate : candidates) {
        if (QDir(candidate).exists() || QFile::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

void MeetingRecorder::onAudioChunk(const QByteArray &pcm)
{
    if (m_recording) {
        m_recognizer.processAudio(pcm);
    }
}

void MeetingRecorder::onFinalText(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    MeetingEntry entry;
    entry.time   = QDateTime::currentDateTime();
    entry.text   = trimmed;
    entry.manual = false;
    m_entries.append(entry);
    emit entryAdded(entry);
}

void MeetingRecorder::onSummaryReceived(const QString &response)
{
    m_summarizing = false;
    emit summaryReady(response);
    emit statusChanged(QStringLiteral("Meeting summary generated."));
}