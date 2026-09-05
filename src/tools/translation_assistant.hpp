#ifndef TITANAI_TRANSLATION_ASSISTANT_HPP
#define TITANAI_TRANSLATION_ASSISTANT_HPP

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QList>
#include <QMap>

// ─────────────────────────────────────────────────────────────
//  Data Structures
// ─────────────────────────────────────────────────────────────

/**
 * @brief Metadata for a supported language.
 */
struct LanguageInfo {
    QString code;        ///< BCP-47 code, e.g. "es", "ja", "zh-CN"
    QString name;        ///< English name, e.g. "Spanish"
    QString nativeName;  ///< Native name, e.g. "Español"
    QString flag;        ///< Unicode flag emoji, e.g. "🇪🇸"
};

/**
 * @brief Tone/register for translation output.
 */
enum class TranslationTone {
    Auto,           ///< Match source tone
    Conversational, ///< Everyday natural speech
    Formal,         ///< Professional / polite register
    Casual,         ///< Informal / slang-friendly
    Technical,      ///< Domain-specific accuracy
    Academic,       ///< Scholarly / essay style
    Poetic          ///< Literary / expressive
};

/**
 * @brief Input parameters for a translation request.
 */
struct TranslationRequest {
    QString text;
    QString sourceLanguage{QStringLiteral("auto")};   ///< "auto" or BCP-47 code
    QString targetLanguage{QStringLiteral("en")};
    TranslationTone tone{TranslationTone::Auto};
    bool provideBreakdown{true};
    bool providePronunciation{true};
};

/**
 * @brief Result returned after a successful translation.
 */
struct TranslationResult {
    QString translatedText;
    QString detectedSourceLanguage; ///< BCP-47 code of the detected source
    QString pronunciation;          ///< Phonetic / romanized guide
    QStringList alternatives;       ///< Alternative translations
    QString grammarNotes;           ///< Grammar / vocabulary breakdown
    QString exampleSentence;        ///< Example usage of the translation
    bool isCached{false};
    bool isOfflineFallback{false};
    bool success{false};
    QString errorMessage;
};

/**
 * @brief A persisted entry in translation history.
 */
struct TranslationHistoryEntry {
    QString id;
    QDateTime timestamp;
    QString sourceText;
    QString translatedText;
    QString sourceLang;
    QString targetLang;
    TranslationTone tone{TranslationTone::Auto};
    bool isFavorite{false};
};

// ─────────────────────────────────────────────────────────────
//  TranslationAssistant
// ─────────────────────────────────────────────────────────────

/**
 * @brief Core translation engine for TitanAI.
 *
 * - Uses the active Ollama model for LLM-based translation.
 * - Maintains an in-memory + on-disk JSON cache.
 * - Maintains persistent JSON translation history.
 * - Provides language detection, phonetics, grammar notes,
 *   alternative suggestions, and an offline phrase fallback.
 */
class TranslationAssistant : public QObject {
    Q_OBJECT

public:
    explicit TranslationAssistant(QObject *parent = nullptr);
    ~TranslationAssistant() override = default;

    // ── Translation API ──────────────────────────────────────

    /** Asynchronously translate text per @p req. Emits translationReady() or translationError(). */
    void translate(const TranslationRequest &req);

    /** Convenience overload: quick-translate @p text to @p targetLang (defaults to "en"). */
    void quickTranslate(const QString &text,
                        const QString &targetLang  = QStringLiteral("en"),
                        TranslationTone tone       = TranslationTone::Auto);

    // ── Language Helpers ─────────────────────────────────────

    /** Returns the full list of supported languages. */
    [[nodiscard]] static QList<LanguageInfo> supportedLanguages();

    /** Returns the LanguageInfo for @p code, or an empty struct if not found. */
    [[nodiscard]] static LanguageInfo languageByCode(const QString &code);

    /** Returns the display name for @p code (English name + flag). */
    [[nodiscard]] static QString languageDisplayName(const QString &code);

    /** Returns a list of BCP-47 codes for all supported languages. */
    [[nodiscard]] static QStringList languageCodes();

    /** Heuristically detect the language of @p text (very fast, regex-based). */
    [[nodiscard]] static QString detectLanguage(const QString &text);

    // ── Tone Helpers ─────────────────────────────────────────

    [[nodiscard]] static QStringList toneNames();
    [[nodiscard]] static QString toneName(TranslationTone tone);
    [[nodiscard]] static TranslationTone toneFromName(const QString &name);

    // ── Prompt Builder ───────────────────────────────────────

    /** Builds the LLM system+user prompt for a given request. */
    [[nodiscard]] static QString buildTranslationPrompt(const TranslationRequest &req);

    /** Parses the raw LLM response into a TranslationResult. */
    [[nodiscard]] static TranslationResult parseTranslationResponse(const QString &llmOutput,
                                                                     const TranslationRequest &req);

    // ── Cache ────────────────────────────────────────────────

    void clearCache();
    [[nodiscard]] int cacheSize() const;

    // ── History ──────────────────────────────────────────────

    [[nodiscard]] QList<TranslationHistoryEntry> history() const;
    [[nodiscard]] QList<TranslationHistoryEntry> searchHistory(const QString &query) const;
    void clearHistory();
    bool removeHistoryEntry(const QString &id);
    bool setFavorite(const QString &id, bool favorite);

    // ── Formatting ───────────────────────────────────────────

    /** Format a TranslationResult as a markdown chat card. */
    [[nodiscard]] static QString formatResultAsMarkdown(const TranslationResult &result,
                                                         const TranslationRequest &req);

    // ── Query Detection ──────────────────────────────────────

    /** Returns true if @p message is a natural-language translation request. */
    [[nodiscard]] static bool isTranslationQuery(const QString &message);

    /** Parses a natural-language chat message into a TranslationRequest. */
    [[nodiscard]] static TranslationRequest parseNaturalLanguageRequest(const QString &message);

signals:
    void translationReady(const TranslationResult &result, const TranslationRequest &req);
    void translationError(const QString &errorMessage, const TranslationRequest &req);
    void translationProgress(const QString &status);

private:
    void onLlmResponse(const QString &response, const TranslationRequest &req);
    void tryOfflineFallback(const TranslationRequest &req);
    void addToHistory(const TranslationRequest &req, const TranslationResult &result);

    QString cacheKey(const TranslationRequest &req) const;
    void loadCache();
    void saveCache();
    void loadHistory();
    void saveHistory();

    [[nodiscard]] QString storagePath() const;

    QMap<QString, TranslationResult>   m_cache;
    QList<TranslationHistoryEntry>     m_history;
    bool m_cacheLoaded{false};
    bool m_historyLoaded{false};
};

#endif // TITANAI_TRANSLATION_ASSISTANT_HPP
