#ifndef TITANAI_MEETING_NOTES_DIALOG_HPP
#define TITANAI_MEETING_NOTES_DIALOG_HPP

#include <QDialog>
#include <QString>

#include "meeting/meeting_recorder.hpp"

class QCloseEvent;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTextBrowser;
class QTimer;
class Agent;

/// Non-modal meeting notes window: live microphone transcription, manual notes,
/// LLM-generated summaries, and Markdown/PDF export.
class MeetingNotesDialog : public QDialog {
    Q_OBJECT

public:
    explicit MeetingNotesDialog(Agent *agent, QWidget *parent = nullptr);
    ~MeetingNotesDialog() override;

signals:
    /// Emitted so the main window can drop the summary into the active chat.
    void sendToChatRequested(const QString &text);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onRecordClicked();
    void onAddNote();
    void onGenerateSummary();
    void onClear();
    void exportMarkdown();
    void exportPdf();
    void sendSummaryToChat();
    void onEntryAdded();
    void onMicLevel(float level);
    void onSummaryReady(const QString &markdown);
    void onSummaryError(const QString &error);
    void onStatus(const QString &message);
    void onError(const QString &message);
    void updateElapsed();

private:
    void setupUi();
    void setupStylesheet();
    void refreshView();
    QString buildHtml() const;
    QString effectiveTitle() const;
    void setRecordingUi(bool recording);

    Agent *m_agent{nullptr};
    MeetingRecorder m_recorder;

    QLabel *m_elapsedLabel{nullptr};
    QLabel *m_entryCountLabel{nullptr};
    QLabel *m_statusLabel{nullptr};
    QLineEdit *m_titleEdit{nullptr};
    QLineEdit *m_noteEdit{nullptr};
    QPushButton *m_recordBtn{nullptr};
    QPushButton *m_summaryBtn{nullptr};
    QPushButton *m_clearBtn{nullptr};
    QPushButton *m_sendToChatBtn{nullptr};
    QTextBrowser *m_browser{nullptr};
    QProgressBar *m_micBar{nullptr};
    QTimer *m_timer{nullptr};

    QString m_summary;
};

#endif // TITANAI_MEETING_NOTES_DIALOG_HPP