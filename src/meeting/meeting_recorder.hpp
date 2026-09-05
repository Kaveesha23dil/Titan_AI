#ifndef TITANAI_MEETING_RECORDER_HPP
#define TITANAI_MEETING_RECORDER_HPP

#include <QDateTime>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>

#include "llm/ollama_client.hpp"
#include "voice/audio_capture.hpp"
#include "voice/speech_recognizer.hpp"

// ─────────────────────────────────────────────────────────────────────────────
//  Data structures
// ─────────────────────────────────────────────────────────────────────────────

/// A single line in the meeting notes: either a transcribed utterance or a
/// note typed by the user.
struct MeetingEntry {
    QDateTime time;          ///< When the entry was recorded
    QString   text;          ///< Entry content
    bool      manual{false}; ///< True when added by the user (typed note)
};

// ─────────────────────────────────────────────────────────────────────────────
//  MeetingRecorder
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Records a meeting from the microphone, transcribes it with Vosk, and
 *        summarizes the transcript with the local Ollama model.
 *
 * Unlike the short-utterance VoiceEngine, recording is open-ended: the user
 * starts and stops it manually. Speech boundaries flush the recognizer so each
 * utterance becomes a timestamped entry, but capture itself never auto-stops.
 * Manual notes can be appended at any time and are merged into the transcript.
 */
class MeetingRecorder : public QObject {
    Q_OBJECT

public:
    explicit MeetingRecorder(QObject *parent = nullptr);
    ~MeetingRecorder() override;

    /// True when a Vosk model is available for offline speech-to-text.
    static bool sttAvailable();

    // ── Recording control ───────────────────────────────────────────────────

    /// Starts live capture + transcription. Emits errorOccurred() and returns
    /// false if no speech recognizer/model is available (typed notes still work).
    bool startRecording();
    void stopRecording();
    bool isRecording() const;
    [[nodiscard]] QDateTime startedTime() const;
    [[nodiscard]] QDateTime endedTime() const;

    // ── Notes ───────────────────────────────────────────────────────────────

    /// Adds a manual note entry (works whether or not recording is active).
    void addNote(const QString &text);
    /// Clears all entries and the current summary state.
    void clear();

    // ── Summarization ───────────────────────────────────────────────────────

    /// Model used for the meeting summary requests (set from the agent).
    void setModel(const QString &model);

    /// Sends the accumulated transcript to the local model for summarization.
    void requestSummary();
    bool isSummarizing() const;

    // ── Transcript access ───────────────────────────────────────────────────

    [[nodiscard]] QList<MeetingEntry> entries() const;
    /// Plain-text transcript of every entry.
    [[nodiscard]] QString transcriptText() const;
    /// Markdown transcript of every entry.
    [[nodiscard]] QString transcriptMarkdown() const;

    // ── Static formatting helpers (kept testable without audio capture) ─────

    /// Builds the LLM prompt for summarizing a meeting transcript.
    static QString buildSummaryPrompt(const QString &transcript);

    /// Formats complete meeting notes (header, summary, transcript) as Markdown.
    static QString formatNotesMarkdown(const QString &title,
                                       const QDateTime &started,
                                       const QDateTime &ended,
                                       const QList<MeetingEntry> &entries,
                                       const QString &summary = QString());

    /// Formats complete meeting notes as a self-contained HTML document.
    static QString formatNotesHtml(const QString &title,
                                   const QDateTime &started,
                                   const QDateTime &ended,
                                   const QList<MeetingEntry> &entries,
                                   const QString &summary = QString());

    /// Minimal safe Markdown → HTML renderer (headings, bullets, **bold**).
    static QString renderMarkdownToHtml(const QString &markdown);

signals:
    void recordingChanged(bool recording);
    void partialTranscript(const QString &text);
    void entryAdded(const MeetingEntry &entry);
    void micLevelChanged(float level);
    void summaryReady(const QString &markdown);
    void summaryError(const QString &error);
    void statusChanged(const QString &message);
    void errorOccurred(const QString &message);

private:
    bool ensureRecognizerStarted();
    QString resolveDefaultModelPath() const;
    void onAudioChunk(const QByteArray &pcm);
    void onFinalText(const QString &text);
    void onSummaryReceived(const QString &response);

    AudioCapture m_capture;
    SpeechRecognizer m_recognizer;
    OllamaClient m_ollama;

    QList<MeetingEntry> m_entries;
    QDateTime m_started;
    QDateTime m_ended;
    QElapsedTimer m_elapsed;
    QString m_model{QStringLiteral("qwen2.5-coder:3b")};
    bool m_recording{false};
    bool m_summarizing{false};
};

#endif // TITANAI_MEETING_RECORDER_HPP