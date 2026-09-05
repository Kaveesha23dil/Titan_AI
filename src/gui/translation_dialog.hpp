#ifndef TITANAI_TRANSLATION_DIALOG_HPP
#define TITANAI_TRANSLATION_DIALOG_HPP

#include <QDialog>
#include "tools/translation_assistant.hpp"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTextBrowser;
class QTextEdit;
class QShortcut;
class QSplitter;
class QCheckBox;
class QTimer;

/**
 * @brief Quick Translation Assistant dialog.
 *
 * Features:
 *  - Source / Target language dropdowns with flag + name
 *  - Language swap button (⇄)
 *  - Translation tone selector
 *  - Rich source text area (with auto-translate option)
 *  - Formatted result area with copy, TTS, and "Send to Chat" actions
 *  - Collapsible grammar / pronunciation / alternatives drawer
 *  - Searchable Translation History panel
 *  - Clipboard / primary-selection grab button
 *  - Keyboard shortcuts: Ctrl+Enter (translate), Ctrl+G (grab), Ctrl+Shift+C (copy), Esc (close)
 */
class TranslationDialog : public QDialog {
    Q_OBJECT

public:
    explicit TranslationDialog(TranslationAssistant *assistant, QWidget *parent = nullptr);
    ~TranslationDialog() override = default;

    /** Pre-populate source text (e.g. from clipboard selection). */
    void setSourceText(const QString &text);

    /** Pre-set target language code. */
    void setTargetLanguage(const QString &langCode);

signals:
    /** Emitted when user clicks "Send to Chat". */
    void sendToChatRequested(const QString &text);
    /** Emitted when TTS playback is requested. */
    void speakRequested(const QString &text, const QString &langCode);

private slots:
    void onTranslateClicked();
    void onGrabSelectionClicked();
    void onSwapLanguages();
    void onCopyTranslation();
    void onSendToChat();
    void onToggleBreakdown();
    void onSourceTextChanged();
    void onAutoTranslateTimer();
    void onHistoryItemClicked(QListWidgetItem *item);
    void onHistorySearchChanged(const QString &text);
    void onClearHistory();
    void onClearSource();
    void onSpeakSource();
    void onSpeakTranslation();
    void onFavoriteClicked();

    void onTranslationReady(const TranslationResult &result, const TranslationRequest &req);
    void onTranslationError(const QString &error, const TranslationRequest &req);
    void onTranslationProgress(const QString &status);

private:
    void setupUi();
    void setupStylesheet();
    void setupShortcuts();
    void populateLanguageCombos();
    void setTranslating(bool busy);
    void displayResult(const TranslationResult &result, const TranslationRequest &req);
    void refreshHistory();
    void showBreakdownSection(const TranslationResult &result);
    void hideBreakdownSection();

    TranslationAssistant *m_assistant{nullptr};

    // State
    TranslationResult m_lastResult;
    TranslationRequest m_lastRequest;
    bool m_autoTranslate{true};

    // Auto-translate debounce timer
    QTimer *m_autoTimer{nullptr};

    // ── Top bar ──
    QComboBox    *m_sourceLangCombo{nullptr};
    QPushButton  *m_swapButton{nullptr};
    QComboBox    *m_targetLangCombo{nullptr};
    QComboBox    *m_toneCombo{nullptr};
    QCheckBox    *m_autoCheck{nullptr};
    QPushButton  *m_grabButton{nullptr};

    // ── Source card ──
    QTextEdit    *m_sourceEdit{nullptr};
    QLabel       *m_charCountLabel{nullptr};
    QLabel       *m_detectedLangLabel{nullptr};
    QPushButton  *m_clearSourceBtn{nullptr};
    QPushButton  *m_speakSourceBtn{nullptr};
    QPushButton  *m_translateBtn{nullptr};

    // ── Result card ──
    QTextBrowser *m_resultBrowser{nullptr};
    QLabel       *m_statusLabel{nullptr};
    QPushButton  *m_copyBtn{nullptr};
    QPushButton  *m_speakResultBtn{nullptr};
    QPushButton  *m_sendToChatBtn{nullptr};
    QPushButton  *m_favoriteBtn{nullptr};

    // ── Breakdown drawer ──
    QPushButton  *m_toggleBreakdownBtn{nullptr};
    QTextBrowser *m_breakdownBrowser{nullptr};
    bool          m_breakdownVisible{false};

    // ── History panel ──
    QLineEdit    *m_historySearch{nullptr};
    QListWidget  *m_historyList{nullptr};
    QPushButton  *m_clearHistoryBtn{nullptr};
};

#endif // TITANAI_TRANSLATION_DIALOG_HPP
