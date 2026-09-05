#include "tools/translation_assistant.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

// ─────────────────────────────────────────────────────────────
//  Supported Languages Table
// ─────────────────────────────────────────────────────────────
static const QList<LanguageInfo> s_languages = {
    {QStringLiteral("auto"), QStringLiteral("Auto Detect"), QStringLiteral("Auto Detect"), QStringLiteral("🌐")},
    {QStringLiteral("en"),   QStringLiteral("English"),    QStringLiteral("English"),      QStringLiteral("🇬🇧")},
    {QStringLiteral("es"),   QStringLiteral("Spanish"),    QStringLiteral("Español"),       QStringLiteral("🇪🇸")},
    {QStringLiteral("fr"),   QStringLiteral("French"),     QStringLiteral("Français"),      QStringLiteral("🇫🇷")},
    {QStringLiteral("de"),   QStringLiteral("German"),     QStringLiteral("Deutsch"),       QStringLiteral("🇩🇪")},
    {QStringLiteral("it"),   QStringLiteral("Italian"),    QStringLiteral("Italiano"),      QStringLiteral("🇮🇹")},
    {QStringLiteral("pt"),   QStringLiteral("Portuguese"), QStringLiteral("Português"),     QStringLiteral("🇵🇹")},
    {QStringLiteral("ru"),   QStringLiteral("Russian"),    QStringLiteral("Русский"),       QStringLiteral("🇷🇺")},
    {QStringLiteral("zh"),   QStringLiteral("Chinese (Simplified)"),  QStringLiteral("中文(简体)"),  QStringLiteral("🇨🇳")},
    {QStringLiteral("zh-TW"),QStringLiteral("Chinese (Traditional)"), QStringLiteral("中文(繁體)"),  QStringLiteral("🇹🇼")},
    {QStringLiteral("ja"),   QStringLiteral("Japanese"),   QStringLiteral("日本語"),        QStringLiteral("🇯🇵")},
    {QStringLiteral("ko"),   QStringLiteral("Korean"),     QStringLiteral("한국어"),         QStringLiteral("🇰🇷")},
    {QStringLiteral("ar"),   QStringLiteral("Arabic"),     QStringLiteral("العربية"),       QStringLiteral("🇸🇦")},
    {QStringLiteral("hi"),   QStringLiteral("Hindi"),      QStringLiteral("हिन्दी"),        QStringLiteral("🇮🇳")},
    {QStringLiteral("bn"),   QStringLiteral("Bengali"),    QStringLiteral("বাংলা"),          QStringLiteral("🇧🇩")},
    {QStringLiteral("tr"),   QStringLiteral("Turkish"),    QStringLiteral("Türkçe"),        QStringLiteral("🇹🇷")},
    {QStringLiteral("nl"),   QStringLiteral("Dutch"),      QStringLiteral("Nederlands"),    QStringLiteral("🇳🇱")},
    {QStringLiteral("pl"),   QStringLiteral("Polish"),     QStringLiteral("Polski"),        QStringLiteral("🇵🇱")},
    {QStringLiteral("sv"),   QStringLiteral("Swedish"),    QStringLiteral("Svenska"),       QStringLiteral("🇸🇪")},
    {QStringLiteral("da"),   QStringLiteral("Danish"),     QStringLiteral("Dansk"),         QStringLiteral("🇩🇰")},
    {QStringLiteral("no"),   QStringLiteral("Norwegian"),  QStringLiteral("Norsk"),         QStringLiteral("🇳🇴")},
    {QStringLiteral("fi"),   QStringLiteral("Finnish"),    QStringLiteral("Suomi"),         QStringLiteral("🇫🇮")},
    {QStringLiteral("cs"),   QStringLiteral("Czech"),      QStringLiteral("Čeština"),       QStringLiteral("🇨🇿")},
    {QStringLiteral("sk"),   QStringLiteral("Slovak"),     QStringLiteral("Slovenčina"),    QStringLiteral("🇸🇰")},
    {QStringLiteral("ro"),   QStringLiteral("Romanian"),   QStringLiteral("Română"),        QStringLiteral("🇷🇴")},
    {QStringLiteral("hu"),   QStringLiteral("Hungarian"),  QStringLiteral("Magyar"),        QStringLiteral("🇭🇺")},
    {QStringLiteral("el"),   QStringLiteral("Greek"),      QStringLiteral("Ελληνικά"),      QStringLiteral("🇬🇷")},
    {QStringLiteral("uk"),   QStringLiteral("Ukrainian"),  QStringLiteral("Українська"),    QStringLiteral("🇺🇦")},
    {QStringLiteral("he"),   QStringLiteral("Hebrew"),     QStringLiteral("עברית"),         QStringLiteral("🇮🇱")},
    {QStringLiteral("fa"),   QStringLiteral("Persian"),    QStringLiteral("فارسی"),         QStringLiteral("🇮🇷")},
    {QStringLiteral("th"),   QStringLiteral("Thai"),       QStringLiteral("ภาษาไทย"),       QStringLiteral("🇹🇭")},
    {QStringLiteral("vi"),   QStringLiteral("Vietnamese"), QStringLiteral("Tiếng Việt"),    QStringLiteral("🇻🇳")},
    {QStringLiteral("id"),   QStringLiteral("Indonesian"), QStringLiteral("Bahasa Indonesia"), QStringLiteral("🇮🇩")},
    {QStringLiteral("ms"),   QStringLiteral("Malay"),      QStringLiteral("Bahasa Melayu"), QStringLiteral("🇲🇾")},
    {QStringLiteral("tl"),   QStringLiteral("Filipino"),   QStringLiteral("Filipino"),      QStringLiteral("🇵🇭")},
    {QStringLiteral("sw"),   QStringLiteral("Swahili"),    QStringLiteral("Kiswahili"),     QStringLiteral("🇰🇪")},
    {QStringLiteral("ca"),   QStringLiteral("Catalan"),    QStringLiteral("Català"),        QStringLiteral("🏳️")},
    {QStringLiteral("hr"),   QStringLiteral("Croatian"),   QStringLiteral("Hrvatski"),      QStringLiteral("🇭🇷")},
    {QStringLiteral("bg"),   QStringLiteral("Bulgarian"),  QStringLiteral("Български"),     QStringLiteral("🇧🇬")},
    {QStringLiteral("sr"),   QStringLiteral("Serbian"),    QStringLiteral("Српски"),        QStringLiteral("🇷🇸")},
    {QStringLiteral("lt"),   QStringLiteral("Lithuanian"), QStringLiteral("Lietuvių"),      QStringLiteral("🇱🇹")},
    {QStringLiteral("lv"),   QStringLiteral("Latvian"),    QStringLiteral("Latviešu"),      QStringLiteral("🇱🇻")},
    {QStringLiteral("et"),   QStringLiteral("Estonian"),   QStringLiteral("Eesti"),         QStringLiteral("🇪🇪")},
    {QStringLiteral("ta"),   QStringLiteral("Tamil"),      QStringLiteral("தமிழ்"),         QStringLiteral("🇮🇳")},
    {QStringLiteral("te"),   QStringLiteral("Telugu"),     QStringLiteral("తెలుగు"),         QStringLiteral("🇮🇳")},
    {QStringLiteral("mr"),   QStringLiteral("Marathi"),    QStringLiteral("मराठी"),          QStringLiteral("🇮🇳")},
    {QStringLiteral("ur"),   QStringLiteral("Urdu"),       QStringLiteral("اردو"),          QStringLiteral("🇵🇰")},
    {QStringLiteral("af"),   QStringLiteral("Afrikaans"),  QStringLiteral("Afrikaans"),     QStringLiteral("🇿🇦")},
    {QStringLiteral("la"),   QStringLiteral("Latin"),      QStringLiteral("Latina"),        QStringLiteral("🏛️")},
};

// ─────────────────────────────────────────────────────────────
//  Offline phrase fallback (tiny built-in dictionary)
// ─────────────────────────────────────────────────────────────
using PhraseMap = QMap<QString, QString>;

static QMap<QString, PhraseMap> buildOfflinePhraseBook()
{
    QMap<QString, PhraseMap> book;
    // Format: book["en->es"]["hello"] = "hola"
    PhraseMap enEs;
    enEs[QStringLiteral("hello")]         = QStringLiteral("hola");
    enEs[QStringLiteral("goodbye")]       = QStringLiteral("adiós");
    enEs[QStringLiteral("thank you")]     = QStringLiteral("gracias");
    enEs[QStringLiteral("yes")]           = QStringLiteral("sí");
    enEs[QStringLiteral("no")]            = QStringLiteral("no");
    enEs[QStringLiteral("please")]        = QStringLiteral("por favor");
    enEs[QStringLiteral("sorry")]         = QStringLiteral("lo siento");
    enEs[QStringLiteral("good morning")]  = QStringLiteral("buenos días");
    enEs[QStringLiteral("good night")]    = QStringLiteral("buenas noches");
    enEs[QStringLiteral("how are you")]   = QStringLiteral("¿cómo estás?");
    book[QStringLiteral("en->es")] = enEs;

    PhraseMap enFr;
    enFr[QStringLiteral("hello")]         = QStringLiteral("bonjour");
    enFr[QStringLiteral("goodbye")]       = QStringLiteral("au revoir");
    enFr[QStringLiteral("thank you")]     = QStringLiteral("merci");
    enFr[QStringLiteral("yes")]           = QStringLiteral("oui");
    enFr[QStringLiteral("no")]            = QStringLiteral("non");
    enFr[QStringLiteral("please")]        = QStringLiteral("s'il vous plaît");
    enFr[QStringLiteral("sorry")]         = QStringLiteral("désolé");
    enFr[QStringLiteral("good morning")]  = QStringLiteral("bonjour");
    enFr[QStringLiteral("good night")]    = QStringLiteral("bonne nuit");
    enFr[QStringLiteral("how are you")]   = QStringLiteral("comment allez-vous ?");
    book[QStringLiteral("en->fr")] = enFr;

    PhraseMap enDe;
    enDe[QStringLiteral("hello")]         = QStringLiteral("hallo");
    enDe[QStringLiteral("goodbye")]       = QStringLiteral("auf Wiedersehen");
    enDe[QStringLiteral("thank you")]     = QStringLiteral("danke");
    enDe[QStringLiteral("yes")]           = QStringLiteral("ja");
    enDe[QStringLiteral("no")]            = QStringLiteral("nein");
    enDe[QStringLiteral("please")]        = QStringLiteral("bitte");
    enDe[QStringLiteral("sorry")]         = QStringLiteral("Entschuldigung");
    enDe[QStringLiteral("good morning")]  = QStringLiteral("guten Morgen");
    enDe[QStringLiteral("good night")]    = QStringLiteral("gute Nacht");
    enDe[QStringLiteral("how are you")]   = QStringLiteral("wie geht es Ihnen?");
    book[QStringLiteral("en->de")] = enDe;

    PhraseMap enJa;
    enJa[QStringLiteral("hello")]         = QStringLiteral("こんにちは");
    enJa[QStringLiteral("goodbye")]       = QStringLiteral("さようなら");
    enJa[QStringLiteral("thank you")]     = QStringLiteral("ありがとう");
    enJa[QStringLiteral("yes")]           = QStringLiteral("はい");
    enJa[QStringLiteral("no")]            = QStringLiteral("いいえ");
    enJa[QStringLiteral("please")]        = QStringLiteral("お願いします");
    enJa[QStringLiteral("sorry")]         = QStringLiteral("ごめんなさい");
    enJa[QStringLiteral("good morning")]  = QStringLiteral("おはようございます");
    enJa[QStringLiteral("good night")]    = QStringLiteral("おやすみなさい");
    enJa[QStringLiteral("how are you")]   = QStringLiteral("お元気ですか？");
    book[QStringLiteral("en->ja")] = enJa;

    PhraseMap enZh;
    enZh[QStringLiteral("hello")]         = QStringLiteral("你好");
    enZh[QStringLiteral("goodbye")]       = QStringLiteral("再见");
    enZh[QStringLiteral("thank you")]     = QStringLiteral("谢谢");
    enZh[QStringLiteral("yes")]           = QStringLiteral("是");
    enZh[QStringLiteral("no")]            = QStringLiteral("不");
    enZh[QStringLiteral("please")]        = QStringLiteral("请");
    enZh[QStringLiteral("sorry")]         = QStringLiteral("对不起");
    enZh[QStringLiteral("good morning")]  = QStringLiteral("早上好");
    enZh[QStringLiteral("good night")]    = QStringLiteral("晚安");
    enZh[QStringLiteral("how are you")]   = QStringLiteral("你好吗？");
    book[QStringLiteral("en->zh")] = enZh;

    return book;
}

static const QMap<QString, PhraseMap> s_phraseBook = buildOfflinePhraseBook();

// ─────────────────────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────────────────────
TranslationAssistant::TranslationAssistant(QObject *parent)
    : QObject(parent)
{
    loadCache();
    loadHistory();
}

// ─────────────────────────────────────────────────────────────
//  Language Helpers
// ─────────────────────────────────────────────────────────────
QList<LanguageInfo> TranslationAssistant::supportedLanguages()
{
    return s_languages;
}

LanguageInfo TranslationAssistant::languageByCode(const QString &code)
{
    for (const LanguageInfo &info : s_languages) {
        if (info.code.compare(code, Qt::CaseInsensitive) == 0)
            return info;
    }
    return {};
}

QString TranslationAssistant::languageDisplayName(const QString &code)
{
    const LanguageInfo info = languageByCode(code);
    if (info.code.isEmpty())
        return code;
    return QStringLiteral("%1 %2").arg(info.flag, info.name);
}

QStringList TranslationAssistant::languageCodes()
{
    QStringList codes;
    codes.reserve(s_languages.size());
    for (const LanguageInfo &info : s_languages)
        codes << info.code;
    return codes;
}

QString TranslationAssistant::detectLanguage(const QString &text)
{
    // Fast heuristic: check for non-Latin Unicode blocks
    for (const QChar &ch : text) {
        uint32_t u = ch.unicode();
        if (u >= 0x3040 && u <= 0x30FF)  return QStringLiteral("ja");   // Hiragana/Katakana
        if (u >= 0x4E00 && u <= 0x9FFF)  return QStringLiteral("zh");   // CJK Unified
        if (u >= 0xAC00 && u <= 0xD7A3)  return QStringLiteral("ko");   // Hangul
        if (u >= 0x0400 && u <= 0x04FF)  return QStringLiteral("ru");   // Cyrillic
        if (u >= 0x0600 && u <= 0x06FF)  return QStringLiteral("ar");   // Arabic
        if (u >= 0x0900 && u <= 0x097F)  return QStringLiteral("hi");   // Devanagari
        if (u >= 0x0E00 && u <= 0x0E7F)  return QStringLiteral("th");   // Thai
        if (u >= 0x0370 && u <= 0x03FF)  return QStringLiteral("el");   // Greek
        if (u >= 0x05D0 && u <= 0x05EA)  return QStringLiteral("he");   // Hebrew
    }
    return QStringLiteral("en"); // Default
}

// ─────────────────────────────────────────────────────────────
//  Tone Helpers
// ─────────────────────────────────────────────────────────────
QStringList TranslationAssistant::toneNames()
{
    return {
        QStringLiteral("Auto"),
        QStringLiteral("Conversational"),
        QStringLiteral("Formal"),
        QStringLiteral("Casual"),
        QStringLiteral("Technical"),
        QStringLiteral("Academic"),
        QStringLiteral("Poetic")
    };
}

QString TranslationAssistant::toneName(TranslationTone tone)
{
    switch (tone) {
    case TranslationTone::Conversational: return QStringLiteral("Conversational");
    case TranslationTone::Formal:         return QStringLiteral("Formal");
    case TranslationTone::Casual:         return QStringLiteral("Casual");
    case TranslationTone::Technical:      return QStringLiteral("Technical");
    case TranslationTone::Academic:       return QStringLiteral("Academic");
    case TranslationTone::Poetic:         return QStringLiteral("Poetic");
    default:                              return QStringLiteral("Auto");
    }
}

TranslationTone TranslationAssistant::toneFromName(const QString &name)
{
    if (name.compare(QStringLiteral("Conversational"), Qt::CaseInsensitive) == 0) return TranslationTone::Conversational;
    if (name.compare(QStringLiteral("Formal"),         Qt::CaseInsensitive) == 0) return TranslationTone::Formal;
    if (name.compare(QStringLiteral("Casual"),         Qt::CaseInsensitive) == 0) return TranslationTone::Casual;
    if (name.compare(QStringLiteral("Technical"),      Qt::CaseInsensitive) == 0) return TranslationTone::Technical;
    if (name.compare(QStringLiteral("Academic"),       Qt::CaseInsensitive) == 0) return TranslationTone::Academic;
    if (name.compare(QStringLiteral("Poetic"),         Qt::CaseInsensitive) == 0) return TranslationTone::Poetic;
    return TranslationTone::Auto;
}

// ─────────────────────────────────────────────────────────────
//  Prompt Builder
// ─────────────────────────────────────────────────────────────
QString TranslationAssistant::buildTranslationPrompt(const TranslationRequest &req)
{
    const QString targetLangName = languageByCode(req.targetLanguage).name.isEmpty()
                                   ? req.targetLanguage
                                   : languageByCode(req.targetLanguage).name;
    const QString sourceLangClause = (req.sourceLanguage == QLatin1String("auto"))
                                     ? QStringLiteral("auto-detect the source language")
                                     : QStringLiteral("source language: %1").arg(languageByCode(req.sourceLanguage).name);

    QString toneClause;
    if (req.tone != TranslationTone::Auto)
        toneClause = QStringLiteral(" Use a %1 tone.").arg(toneName(req.tone));

    QString breakdownSection;
    if (req.provideBreakdown) {
        breakdownSection = QStringLiteral(
            "\n\nThen output the following sections (only if applicable), each prefixed exactly as shown:\n"
            "PRONUNCIATION: <phonetic / romanized pronunciation of the translation>\n"
            "ALTERNATIVES: <comma-separated list of 2-3 alternative translations>\n"
            "GRAMMAR_NOTES: <brief grammar or vocabulary explanation in English, max 2 sentences>\n"
            "EXAMPLE: <a natural example sentence using the translated word/phrase in the target language>\n"
            "DETECTED_SOURCE: <the BCP-47 code of the detected source language, e.g. en, es, zh>"
        );
    }

    return QStringLiteral(
        "You are an expert multilingual translator. "
        "Translate the following text into %1 (%2).%3\n\n"
        "IMPORTANT: Output ONLY the translated text on the first line. "
        "Do NOT include the original text, explanations before the translation, "
        "or any preamble. Be precise and natural.%4\n\n"
        "Text to translate:\n%5"
    ).arg(targetLangName, req.targetLanguage, toneClause, breakdownSection, req.text);
}

// ─────────────────────────────────────────────────────────────
//  Response Parser
// ─────────────────────────────────────────────────────────────
TranslationResult TranslationAssistant::parseTranslationResponse(const QString &llmOutput,
                                                                   const TranslationRequest &req)
{
    TranslationResult result;
    result.success = true;

    const QStringList lines = llmOutput.split(QLatin1Char('\n'));

    // First non-empty line is the translation
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            result.translatedText = trimmed;
            break;
        }
    }

    if (result.translatedText.isEmpty()) {
        result.success = false;
        result.errorMessage = QStringLiteral("Empty translation response.");
        return result;
    }

    // Parse structured sections
    static const QRegularExpression rePronunciation(
        QStringLiteral(R"(^PRONUNCIATION:\s*(.+)$)"), QRegularExpression::MultilineOption);
    static const QRegularExpression reAlternatives(
        QStringLiteral(R"(^ALTERNATIVES:\s*(.+)$)"), QRegularExpression::MultilineOption);
    static const QRegularExpression reGrammar(
        QStringLiteral(R"(^GRAMMAR_NOTES:\s*(.+)$)"), QRegularExpression::MultilineOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression reExample(
        QStringLiteral(R"(^EXAMPLE:\s*(.+)$)"), QRegularExpression::MultilineOption);
    static const QRegularExpression reDetected(
        QStringLiteral(R"(^DETECTED_SOURCE:\s*(.+)$)"), QRegularExpression::MultilineOption);

    auto extract = [&](const QRegularExpression &re) -> QString {
        const auto m = re.match(llmOutput);
        return m.hasMatch() ? m.captured(1).trimmed() : QString();
    };

    result.pronunciation           = extract(rePronunciation);
    result.grammarNotes            = extract(reGrammar);
    result.exampleSentence         = extract(reExample);
    result.detectedSourceLanguage  = extract(reDetected);

    const QString altsRaw = extract(reAlternatives);
    if (!altsRaw.isEmpty()) {
        const QStringList parts = altsRaw.split(QLatin1Char(','));
        for (const QString &p : parts) {
            const QString trimmed = p.trimmed();
            if (!trimmed.isEmpty())
                result.alternatives << trimmed;
        }
    }

    // Fallback detected source
    if (result.detectedSourceLanguage.isEmpty())
        result.detectedSourceLanguage = (req.sourceLanguage == QLatin1String("auto"))
                                         ? detectLanguage(req.text)
                                         : req.sourceLanguage;

    return result;
}

// ─────────────────────────────────────────────────────────────
//  Translate
// ─────────────────────────────────────────────────────────────
void TranslationAssistant::translate(const TranslationRequest &req)
{
    if (req.text.trimmed().isEmpty()) {
        TranslationResult err;
        err.success = false;
        err.errorMessage = QStringLiteral("No text provided for translation.");
        emit translationError(err.errorMessage, req);
        return;
    }

    // Check cache first
    const QString key = cacheKey(req);
    if (m_cache.contains(key)) {
        TranslationResult cached = m_cache[key];
        cached.isCached = true;
        emit translationProgress(QStringLiteral("⚡ Loaded from cache"));
        emit translationReady(cached, req);
        return;
    }

    // Use offline fallback for very short common phrases
    const QString lower = req.text.trimmed().toLower();
    const QString pairKey = QStringLiteral("%1->%2").arg(
        (req.sourceLanguage == QLatin1String("auto")) ? QStringLiteral("en") : req.sourceLanguage,
        req.targetLanguage);

    if (s_phraseBook.contains(pairKey)) {
        const PhraseMap &phrases = s_phraseBook[pairKey];
        if (phrases.contains(lower)) {
            TranslationResult offline;
            offline.translatedText            = phrases[lower];
            offline.detectedSourceLanguage    = QStringLiteral("en");
            offline.isOfflineFallback         = true;
            offline.success                   = true;
            m_cache[key] = offline;
            saveCache();
            addToHistory(req, offline);
            emit translationProgress(QStringLiteral("📖 Found in offline phrasebook"));
            emit translationReady(offline, req);
            return;
        }
    }

    emit translationProgress(QStringLiteral("🔄 Translating via AI model…"));

    const QString prompt = buildTranslationPrompt(req);

    // Use a single-shot QNetworkAccessManager request through OllamaClient
    // We fire a QProcess-based curl call as a lightweight non-blocking approach
    // to avoid tight coupling with the global OllamaClient streaming system.
    // The result is parsed and emitted back on the main thread via QTimer::singleShot.
    auto *process = new QProcess(this);
    const QStringList args = {
        QStringLiteral("-s"),
        QStringLiteral("-X"), QStringLiteral("POST"),
        QStringLiteral("http://127.0.0.1:11434/api/generate"),
        QStringLiteral("-H"), QStringLiteral("Content-Type: application/json"),
        QStringLiteral("-d"), QJsonDocument(QJsonObject{
            {QStringLiteral("model"),  QStringLiteral("qwen2.5-coder:3b")},
            {QStringLiteral("prompt"), prompt},
            {QStringLiteral("stream"), false}
        }).toJson(QJsonDocument::Compact)
    };

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process, req, key](int exitCode, QProcess::ExitStatus) {
        const QByteArray raw = process->readAllStandardOutput();
        process->deleteLater();

        if (exitCode != 0 || raw.isEmpty()) {
            tryOfflineFallback(req);
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(raw);
        const QString responseText = doc.object().value(QLatin1String("response")).toString().trimmed();

        if (responseText.isEmpty()) {
            tryOfflineFallback(req);
            return;
        }

        TranslationResult result = parseTranslationResponse(responseText, req);
        if (!result.success) {
            tryOfflineFallback(req);
            return;
        }

        m_cache[key] = result;
        saveCache();
        addToHistory(req, result);
        emit translationReady(result, req);
    });

    process->start(QStringLiteral("curl"), args);
}

void TranslationAssistant::quickTranslate(const QString &text,
                                           const QString &targetLang,
                                           TranslationTone tone)
{
    TranslationRequest req;
    req.text                 = text;
    req.sourceLanguage       = QStringLiteral("auto");
    req.targetLanguage       = targetLang;
    req.tone                 = tone;
    req.provideBreakdown     = true;
    req.providePronunciation = true;
    translate(req);
}

void TranslationAssistant::tryOfflineFallback(const TranslationRequest &req)
{
    TranslationResult fallback;
    fallback.success             = false;
    fallback.isOfflineFallback   = true;
    fallback.errorMessage        = QStringLiteral(
        "AI model unavailable. Common phrases can be found in the offline phrasebook.");
    emit translationError(fallback.errorMessage, req);
}

// ─────────────────────────────────────────────────────────────
//  Markdown Formatter
// ─────────────────────────────────────────────────────────────
QString TranslationAssistant::formatResultAsMarkdown(const TranslationResult &result,
                                                      const TranslationRequest &req)
{
    if (!result.success)
        return QStringLiteral("❌ **Translation Error:** %1").arg(result.errorMessage);

    const QString srcFlag  = languageByCode(req.sourceLanguage).flag.isEmpty()
                             ? QStringLiteral("🌐") : languageByCode(req.sourceLanguage).flag;
    const QString tgtLang  = languageDisplayName(req.targetLanguage);
    const QString srcLabel = (req.sourceLanguage == QLatin1String("auto") && !result.detectedSourceLanguage.isEmpty())
                             ? languageDisplayName(result.detectedSourceLanguage)
                             : languageDisplayName(req.sourceLanguage);

    QString md;
    md += QStringLiteral("## 🌐 Translation\n\n");
    md += QStringLiteral("| | |\n|---|---|\n");
    md += QStringLiteral("| **Source** | %1 `%2` |\n").arg(srcFlag, req.text.left(200));
    md += QStringLiteral("| **Language** | %1 → %2 |\n").arg(srcLabel, tgtLang);
    if (req.tone != TranslationTone::Auto)
        md += QStringLiteral("| **Tone** | %1 |\n").arg(toneName(req.tone));
    if (result.isCached)
        md += QStringLiteral("| **Source** | ⚡ Cached |\n");
    else if (result.isOfflineFallback)
        md += QStringLiteral("| **Source** | 📖 Offline Phrasebook |\n");

    md += QStringLiteral("\n### ✅ Translation\n> %1\n\n").arg(result.translatedText);

    if (!result.pronunciation.isEmpty())
        md += QStringLiteral("**🔊 Pronunciation:** *%1*\n\n").arg(result.pronunciation);

    if (!result.alternatives.isEmpty())
        md += QStringLiteral("**💡 Alternatives:** %1\n\n").arg(result.alternatives.join(QStringLiteral(", ")));

    if (!result.grammarNotes.isEmpty())
        md += QStringLiteral("**📚 Grammar Notes:** %1\n\n").arg(result.grammarNotes);

    if (!result.exampleSentence.isEmpty())
        md += QStringLiteral("**💬 Example:** *%1*\n").arg(result.exampleSentence);

    return md;
}

// ─────────────────────────────────────────────────────────────
//  Natural Language Query Detection & Parsing
// ─────────────────────────────────────────────────────────────
bool TranslationAssistant::isTranslationQuery(const QString &message)
{
    const QString lower = message.toLower();
    static const QStringList triggers = {
        QStringLiteral("translate"),
        QStringLiteral("translation"),
        QStringLiteral("how do you say"),
        QStringLiteral("how to say"),
        QStringLiteral("what is the"),
        QStringLiteral("in spanish"),
        QStringLiteral("in french"),
        QStringLiteral("in german"),
        QStringLiteral("in japanese"),
        QStringLiteral("in chinese"),
        QStringLiteral("in korean"),
        QStringLiteral("in arabic"),
        QStringLiteral("in russian"),
        QStringLiteral("in italian"),
        QStringLiteral("in portuguese"),
        QStringLiteral("in hindi"),
        QStringLiteral("quick translate"),
    };

    for (const QString &trigger : triggers) {
        if (lower.contains(trigger))
            return true;
    }
    return false;
}

TranslationRequest TranslationAssistant::parseNaturalLanguageRequest(const QString &message)
{
    TranslationRequest req;
    req.sourceLanguage   = QStringLiteral("auto");
    req.targetLanguage   = QStringLiteral("en");
    req.tone             = TranslationTone::Auto;
    req.provideBreakdown = true;

    const QString lower = message.toLower();

    // Detect target language from patterns like "in German", "to French", "into Spanish"
    static const QMap<QString, QString> langKeywords = {
        {QStringLiteral("english"),    QStringLiteral("en")},
        {QStringLiteral("spanish"),    QStringLiteral("es")},
        {QStringLiteral("french"),     QStringLiteral("fr")},
        {QStringLiteral("german"),     QStringLiteral("de")},
        {QStringLiteral("italian"),    QStringLiteral("it")},
        {QStringLiteral("portuguese"), QStringLiteral("pt")},
        {QStringLiteral("russian"),    QStringLiteral("ru")},
        {QStringLiteral("chinese"),    QStringLiteral("zh")},
        {QStringLiteral("japanese"),   QStringLiteral("ja")},
        {QStringLiteral("korean"),     QStringLiteral("ko")},
        {QStringLiteral("arabic"),     QStringLiteral("ar")},
        {QStringLiteral("hindi"),      QStringLiteral("hi")},
        {QStringLiteral("turkish"),    QStringLiteral("tr")},
        {QStringLiteral("dutch"),      QStringLiteral("nl")},
        {QStringLiteral("polish"),     QStringLiteral("pl")},
        {QStringLiteral("swedish"),    QStringLiteral("sv")},
        {QStringLiteral("greek"),      QStringLiteral("el")},
        {QStringLiteral("hebrew"),     QStringLiteral("he")},
        {QStringLiteral("thai"),       QStringLiteral("th")},
        {QStringLiteral("vietnamese"), QStringLiteral("vi")},
        {QStringLiteral("indonesian"), QStringLiteral("id")},
        {QStringLiteral("latin"),      QStringLiteral("la")},
        {QStringLiteral("swahili"),    QStringLiteral("sw")},
    };

    for (auto it = langKeywords.constBegin(); it != langKeywords.constEnd(); ++it) {
        if (lower.contains(it.key())) {
            req.targetLanguage = it.value();
            break;
        }
    }

    // Extract the text to translate (heuristics for common patterns)
    // Pattern 1: translate "text" to Language
    static const QRegularExpression reQuoted(QStringLiteral("[\"'](.+?)[\"']"));
    auto m = reQuoted.match(message);
    if (m.hasMatch()) {
        req.text = m.captured(1).trimmed();
        return req;
    }

    // Pattern 2: translate this into/to Language: text
    static const QRegularExpression reColon(
        QStringLiteral("(?:translate|say|translation)[^:]*:\\s*(.+)$"),
        QRegularExpression::CaseInsensitiveOption);
    m = reColon.match(message);
    if (m.hasMatch()) {
        req.text = m.captured(1).trimmed();
        return req;
    }

    // Pattern 3: how do you say X in Language / how to say X in Language
    static const QRegularExpression reHowToSay(
        QStringLiteral("(?:how (?:do you|to) say)\\s+(.+?)\\s+in\\s+\\w+"),
        QRegularExpression::CaseInsensitiveOption);
    m = reHowToSay.match(message);
    if (m.hasMatch()) {
        req.text = m.captured(1).trimmed();
        return req;
    }

    // Pattern 4: quick translate to Language: text
    static const QRegularExpression reQuick(
        QStringLiteral("quick translate[^:]*:\\s*(.+)$"),
        QRegularExpression::CaseInsensitiveOption);
    m = reQuick.match(message);
    if (m.hasMatch()) {
        req.text = m.captured(1).trimmed();
        return req;
    }

    // Fallback: entire message minus the command prefix
    static const QRegularExpression reStripCommand(
        QStringLiteral("^(?:translate|translation|quick translate)\\s+(?:this\\s+)?(?:text\\s+)?(?:(?:to|into|in)\\s+\\w+\\s*:?\\s*)?"),
        QRegularExpression::CaseInsensitiveOption);
    req.text = message;
    req.text.remove(reStripCommand);
    req.text = req.text.trimmed();

    return req;
}

// ─────────────────────────────────────────────────────────────
//  Cache
// ─────────────────────────────────────────────────────────────
QString TranslationAssistant::cacheKey(const TranslationRequest &req) const
{
    return QStringLiteral("%1|%2|%3|%4")
        .arg(req.text, req.sourceLanguage, req.targetLanguage)
        .arg(static_cast<int>(req.tone));
}

void TranslationAssistant::clearCache()
{
    m_cache.clear();
    QFile::remove(storagePath() + QStringLiteral("/translation_cache.json"));
}

int TranslationAssistant::cacheSize() const
{
    return static_cast<int>(m_cache.size());
}

void TranslationAssistant::loadCache()
{
    if (m_cacheLoaded) return;
    m_cacheLoaded = true;

    QFile f(storagePath() + QStringLiteral("/translation_cache.json"));
    if (!f.open(QIODevice::ReadOnly)) return;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonArray arr = doc.array();
    for (const QJsonValue &v : arr) {
        const QJsonObject obj = v.toObject();
        const QString key = obj.value(QLatin1String("key")).toString();
        TranslationResult res;
        res.translatedText           = obj.value(QLatin1String("translated")).toString();
        res.detectedSourceLanguage   = obj.value(QLatin1String("detected")).toString();
        res.pronunciation            = obj.value(QLatin1String("pronunciation")).toString();
        res.grammarNotes             = obj.value(QLatin1String("grammar")).toString();
        res.exampleSentence          = obj.value(QLatin1String("example")).toString();
        res.success                  = true;
        const QJsonArray alts = obj.value(QLatin1String("alternatives")).toArray();
        for (const QJsonValue &a : alts)
            res.alternatives << a.toString();
        if (!key.isEmpty() && !res.translatedText.isEmpty())
            m_cache[key] = res;
    }
}

void TranslationAssistant::saveCache()
{
    QDir().mkpath(storagePath());
    QJsonArray arr;
    for (auto it = m_cache.constBegin(); it != m_cache.constEnd(); ++it) {
        QJsonObject obj;
        obj[QStringLiteral("key")]           = it.key();
        obj[QStringLiteral("translated")]    = it.value().translatedText;
        obj[QStringLiteral("detected")]      = it.value().detectedSourceLanguage;
        obj[QStringLiteral("pronunciation")] = it.value().pronunciation;
        obj[QStringLiteral("grammar")]       = it.value().grammarNotes;
        obj[QStringLiteral("example")]       = it.value().exampleSentence;
        QJsonArray alts;
        for (const QString &a : it.value().alternatives) alts.append(a);
        obj[QStringLiteral("alternatives")]  = alts;
        arr.append(obj);
    }
    QFile f(storagePath() + QStringLiteral("/translation_cache.json"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

// ─────────────────────────────────────────────────────────────
//  History
// ─────────────────────────────────────────────────────────────
QList<TranslationHistoryEntry> TranslationAssistant::history() const
{
    return m_history;
}

QList<TranslationHistoryEntry> TranslationAssistant::searchHistory(const QString &query) const
{
    const QString lower = query.toLower();
    QList<TranslationHistoryEntry> results;
    for (const TranslationHistoryEntry &e : m_history) {
        if (e.sourceText.toLower().contains(lower) ||
            e.translatedText.toLower().contains(lower) ||
            e.sourceLang.toLower().contains(lower) ||
            e.targetLang.toLower().contains(lower)) {
            results << e;
        }
    }
    return results;
}

void TranslationAssistant::clearHistory()
{
    m_history.clear();
    QFile::remove(storagePath() + QStringLiteral("/translation_history.json"));
}

bool TranslationAssistant::removeHistoryEntry(const QString &id)
{
    for (int i = 0; i < m_history.size(); ++i) {
        if (m_history[i].id == id) {
            m_history.removeAt(i);
            saveHistory();
            return true;
        }
    }
    return false;
}

bool TranslationAssistant::setFavorite(const QString &id, bool favorite)
{
    for (TranslationHistoryEntry &e : m_history) {
        if (e.id == id) {
            e.isFavorite = favorite;
            saveHistory();
            return true;
        }
    }
    return false;
}

void TranslationAssistant::addToHistory(const TranslationRequest &req,
                                         const TranslationResult  &result)
{
    if (!result.success || result.translatedText.isEmpty()) return;

    TranslationHistoryEntry entry;
    entry.id             = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.timestamp      = QDateTime::currentDateTime();
    entry.sourceText     = req.text;
    entry.translatedText = result.translatedText;
    entry.sourceLang     = result.detectedSourceLanguage.isEmpty()
                           ? req.sourceLanguage : result.detectedSourceLanguage;
    entry.targetLang     = req.targetLanguage;
    entry.tone           = req.tone;

    m_history.prepend(entry);

    // Cap at 500 entries
    while (m_history.size() > 500)
        m_history.removeLast();

    saveHistory();
}

void TranslationAssistant::loadHistory()
{
    if (m_historyLoaded) return;
    m_historyLoaded = true;

    QFile f(storagePath() + QStringLiteral("/translation_history.json"));
    if (!f.open(QIODevice::ReadOnly)) return;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonArray arr = doc.array();
    for (const QJsonValue &v : arr) {
        const QJsonObject obj = v.toObject();
        TranslationHistoryEntry e;
        e.id             = obj.value(QLatin1String("id")).toString();
        e.timestamp      = QDateTime::fromString(obj.value(QLatin1String("timestamp")).toString(),
                                                  Qt::ISODate);
        e.sourceText     = obj.value(QLatin1String("sourceText")).toString();
        e.translatedText = obj.value(QLatin1String("translatedText")).toString();
        e.sourceLang     = obj.value(QLatin1String("sourceLang")).toString();
        e.targetLang     = obj.value(QLatin1String("targetLang")).toString();
        e.tone           = static_cast<TranslationTone>(obj.value(QLatin1String("tone")).toInt());
        e.isFavorite     = obj.value(QLatin1String("isFavorite")).toBool();
        if (!e.id.isEmpty())
            m_history << e;
    }
}

void TranslationAssistant::saveHistory()
{
    QDir().mkpath(storagePath());
    QJsonArray arr;
    for (const TranslationHistoryEntry &e : m_history) {
        QJsonObject obj;
        obj[QStringLiteral("id")]            = e.id;
        obj[QStringLiteral("timestamp")]     = e.timestamp.toString(Qt::ISODate);
        obj[QStringLiteral("sourceText")]    = e.sourceText;
        obj[QStringLiteral("translatedText")]= e.translatedText;
        obj[QStringLiteral("sourceLang")]    = e.sourceLang;
        obj[QStringLiteral("targetLang")]    = e.targetLang;
        obj[QStringLiteral("tone")]          = static_cast<int>(e.tone);
        obj[QStringLiteral("isFavorite")]    = e.isFavorite;
        arr.append(obj);
    }
    QFile f(storagePath() + QStringLiteral("/translation_history.json"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QString TranslationAssistant::storagePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
           + QStringLiteral("/TitanAI");
}

#include "moc_translation_assistant.cpp"
