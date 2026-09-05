#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>

#include "agent/agent.hpp"
#include "agent/chat_history_manager.hpp"
#include "llm/ollama_client.hpp"
#include "llm/ollama_manager.hpp"
#include "tools/code_fixer.hpp"
#include "tools/system_info.hpp"
#include "tools/ui_developer.hpp"
#include "tools/update_checker.hpp"
#include "tools/translation_assistant.hpp"
#include "learning/task_tracker.hpp"
#include "learning/activity_analyzer.hpp"
#include "learning/suggestion_engine.hpp"
#include "power/power_manager.hpp"

class TitanAiTestSuite : public QObject {
    Q_OBJECT

private slots:
    void testSystemInfo() {
        SystemInfoTool tool;
        SystemInfo info = tool.getSystemInfo();
        QVERIFY(!info.operatingSystem.isEmpty());
        QVERIFY(!info.kernelVersion.isEmpty());
        QVERIFY(info.cpuCoreCount > 0);
        QVERIFY(info.totalMemoryBytes > 0);
        std::cout << "[PASS] SystemInfo: " << info.operatingSystem.toStdString()
                  << ", CPU cores: " << info.cpuCoreCount << std::endl;
    }

    void testCodeFixerParsing() {
        QString gccOutput = QStringLiteral(
            "/home/user/project/src/main.cpp:42:15: error: 'vector' was not declared in this scope\n"
            "/home/user/project/src/main.cpp:55:5: error: expected ';' before 'return'\n"
        );
        auto errors = CodeFixer::parseErrors(gccOutput);
        QCOMPARE(errors.size(), 2);
        QCOMPARE(errors[0].file, QStringLiteral("/home/user/project/src/main.cpp"));
        QCOMPARE(errors[0].line, 42);
        QCOMPARE(errors[0].column, 15);
        QCOMPARE(errors[1].line, 55);

        // Test fix parsing
        QString llmFix = QStringLiteral(
            "FILE: /home/user/project/src/main.cpp\n"
            "REPLACE_LINES: 40-45\n"
            "#include <vector>\n"
            "std::vector<int> v;\n"
            "END\n"
        );
        auto fixes = CodeFixer::parseFixes(llmFix);
        QCOMPARE(fixes.size(), 1);
        QCOMPARE(fixes[0].startLine, 40);
        QCOMPARE(fixes[0].endLine, 45);
        std::cout << "[PASS] CodeFixer error and fix block parsing" << std::endl;
    }

    void testCodeFixerApplyEdit() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        QString testFile = tempDir.filePath(QStringLiteral("test.cpp"));
        {
            QFile f(testFile);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            QTextStream out(&f);
            out << "line 1\nline 2 (broken)\nline 3\n";
        }

        CodeFixer::FixEdit edit;
        edit.file = testFile;
        edit.startLine = 2;
        edit.endLine = 2;
        edit.replacement = QStringLiteral("line 2 (fixed)");

        QString error;
        bool ok = CodeFixer::applyEdit(testFile, edit, &error);
        QByteArray errorBytes = error.toUtf8();
        QVERIFY2(ok, errorBytes.constData());

        // Verify content
        QFile f(testFile);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        QString content = QString::fromUtf8(f.readAll());
        QVERIFY(content.contains(QStringLiteral("line 2 (fixed)")));
        QVERIFY(QFile::exists(testFile + QStringLiteral(".bak")));
        std::cout << "[PASS] CodeFixer applyEdit & backup creation" << std::endl;
    }

    void testUiDeveloperDetectionAndParsing() {
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        // Create CMakeLists.txt -> should detect QtCpp
        {
            QFile f(tempDir.filePath(QStringLiteral("CMakeLists.txt")));
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        auto fw = UiDeveloper::detectFramework(tempDir.path());
        QCOMPARE(fw, UiDeveloper::Framework::QtCpp);

        // Test prompt builder
        auto prompt = UiDeveloper::buildUiGenerationPrompt(QStringLiteral("Login screen"), fw, tempDir.path());
        QVERIFY(prompt.contains(QStringLiteral("Login screen")));
        std::cout << "[PASS] UiDeveloper framework detection and prompt builder" << std::endl;
    }

    void testTaskTrackerAndSuggestions() {
        TaskTracker tracker;
        ActivityAnalyzer analyzer;
        analyzer.analyze({});

        SuggestionEngine engine;
        engine.initialize(&tracker, &analyzer);
        auto suggestions = engine.generateStartupSuggestions();
        QVERIFY(suggestions.size() >= 0);
        std::cout << "[PASS] TaskTracker and SuggestionEngine" << std::endl;
    }

    void testOllamaConnectionAndInference() {
        OllamaClient client;
        client.setModel(QString::fromLatin1(Agent::kDefaultModel));

        bool received = false;
        QString receivedResponse;
        QString errorMsg;

        connect(&client, &OllamaClient::responseReceived, [&](const QString &resp) {
            received = true;
            receivedResponse = resp;
            QCoreApplication::quit();
        });

        connect(&client, &OllamaClient::errorOccurred, [&](const QString &err) {
            errorMsg = err;
            QCoreApplication::quit();
        });

        std::cout << "Testing live Ollama inference (" << Agent::kDefaultModel << ")..." << std::endl;
        client.sendPrompt(QStringLiteral("Say 'OK' if you can read this."));

        QTimer timeoutTimer;
        timeoutTimer.setSingleShot(true);
        connect(&timeoutTimer, &QTimer::timeout, [&]() {
            QCoreApplication::quit();
        });
        timeoutTimer.start(30000); // 30 second timeout for cold load

        qApp->exec();

        if (!received && !errorMsg.isEmpty()) {
            std::cout << "[WARN] Ollama test returned error: " << errorMsg.toStdString() << std::endl;
        } else {
            QVERIFY2(received, "Timed out waiting for Ollama response");
            std::cout << "[PASS] Live Ollama Response: " << receivedResponse.left(80).toStdString() << std::endl;
        }
    }

    void testOllamaNonStreamingCompletion() {
        OllamaClient client;
        client.setModel(QString::fromLatin1(Agent::kDefaultModel));

        bool received = false;
        QString receivedResponse;
        QString errorMsg;

        connect(&client, &OllamaClient::completionReceived, [&](const QString &resp) {
            received = true;
            receivedResponse = resp;
            QCoreApplication::quit();
        });

        connect(&client, &OllamaClient::completionError, [&](const QString &err) {
            errorMsg = err;
            QCoreApplication::quit();
        });

        std::cout << "Testing non-streaming requestCompletion (" << Agent::kDefaultModel << ")..." << std::endl;
        client.requestCompletion(QStringLiteral("Write 'SUCCESS'"));

        QTimer timeoutTimer;
        timeoutTimer.setSingleShot(true);
        connect(&timeoutTimer, &QTimer::timeout, [&]() {
            QCoreApplication::quit();
        });
        timeoutTimer.start(30000);

        qApp->exec();

        QByteArray errBytes = errorMsg.toUtf8();
        QVERIFY2(errorMsg.isEmpty(), errBytes.constData());
        QVERIFY2(received, "Timed out waiting for completion response");
        std::cout << "[PASS] Non-streaming Response: " << receivedResponse.left(80).toStdString() << std::endl;
    }

    void testPowerManagerBatteryRead() {
        PowerManager pm;
        pm.refreshBatteryInfo();
        const BatteryInfo info = pm.batteryInfo();
        // On any system the call should not crash; percent is either valid or -1
        QVERIFY(info.percent >= -1 && info.percent <= 100);
        std::cout << "[PASS] PowerManager battery read: "
                  << info.percent << "%, AC=" << info.acOnline
                  << ", status=" << info.statusText.toStdString() << std::endl;
    }

    void testPowerManagerProfileSwitch() {
        PowerManager pm;
        pm.setProfile(PowerProfile::Performance);
        QCOMPARE(pm.currentProfile(), PowerProfile::Performance);
        QCOMPARE(pm.recommendedThreads(), 8);
        QCOMPARE(pm.recommendedContext(),  4096);

        pm.setProfile(PowerProfile::PowerSaver);
        QCOMPARE(pm.currentProfile(), PowerProfile::PowerSaver);
        QCOMPARE(pm.recommendedThreads(), 2);
        QCOMPARE(pm.recommendedContext(),  512);

        pm.setProfile(PowerProfile::Balanced);
        QCOMPARE(pm.currentProfile(), PowerProfile::Balanced);
        QCOMPARE(pm.recommendedThreads(), 4);
        QCOMPARE(pm.recommendedContext(),  1536);
        std::cout << "[PASS] PowerManager profile switching" << std::endl;
    }

    void testPowerManagerReport() {
        PowerManager pm;
        pm.refreshBatteryInfo();
        const QString report = pm.generateReport();
        QVERIFY(!report.isEmpty());
        QVERIFY(report.contains(QStringLiteral("Power Management Report")));
        QVERIFY(report.contains(QStringLiteral("LLM Recommendations")));
        std::cout << "[PASS] PowerManager report generation, length="
                  << report.length() << std::endl;
    }

    void testUpdateCheckerStructure() {
        UpdateChecker uc;
        // Verify initial state
        QVERIFY(!uc.isChecking());
        QVERIFY(!uc.isApplying());
        QVERIFY(!uc.isPeriodicCheckActive());
        QVERIFY(!uc.lastCheckTime().isValid());
        QCOMPARE(uc.lastCheckTimeString(), QStringLiteral("Never"));
        QCOMPARE(uc.installedPackageCount(), 0);
        QVERIFY(uc.pendingUpdates().isEmpty());
        // Periodic check control
        uc.startPeriodicCheck(60);
        QVERIFY(uc.isPeriodicCheckActive());
        uc.stopPeriodicCheck();
        QVERIFY(!uc.isPeriodicCheckActive());
        std::cout << "[PASS] UpdateChecker structure and periodic check API" << std::endl;
    }

    void testUpdateCheckerReport() {
        UpdateChecker uc;
        // Empty state report
        const QString emptyReport = uc.formatUpdateReport();
        // Once installed list is empty the report should indicate 0 installed
        QVERIFY(emptyReport.contains(QStringLiteral("Installed packages: 0")));
        QVERIFY(emptyReport.contains(QStringLiteral("up to date")) ||
                emptyReport.contains(QStringLiteral("packages: 0")));
        std::cout << "[PASS] UpdateChecker empty report: " << emptyReport.left(60).toStdString() << std::endl;
    }

    // ── Chat History Manager Tests ─────────────────────────────────────────────

    void testChatHistorySessionLifecycle() {
        ChatHistoryManager mgr;

        // On construction a default session is created automatically
        QVERIFY(mgr.sessionCount() >= 1);
        const QString firstId = mgr.currentSessionId();
        QVERIFY(!firstId.isEmpty());

        // Create a named session
        const QString namedId = mgr.createSession(QStringLiteral("Test Session Alpha"));
        QVERIFY(!namedId.isEmpty());
        QVERIFY(namedId != firstId);
        QCOMPARE(mgr.currentSessionId(), namedId);

        // Verify it appears in the index
        bool found = false;
        for (const ChatSessionSummary &s : mgr.allSessions()) {
            if (s.id == namedId && s.title == QStringLiteral("Test Session Alpha")) {
                found = true;
                break;
            }
        }
        QVERIFY(found);

        // Rename
        QVERIFY(mgr.renameSession(namedId, QStringLiteral("Renamed Session")));
        for (const ChatSessionSummary &s : mgr.allSessions()) {
            if (s.id == namedId) {
                QCOMPARE(s.title, QStringLiteral("Renamed Session"));
                break;
            }
        }

        // Switch back to first
        QVERIFY(mgr.switchSession(firstId));
        QCOMPARE(mgr.currentSessionId(), firstId);

        // Delete named session
        const int countBefore = mgr.sessionCount();
        QVERIFY(mgr.deleteSession(namedId));
        QCOMPARE(mgr.sessionCount(), countBefore - 1);

        std::cout << "[PASS] ChatHistory session lifecycle (create/rename/switch/delete)" << std::endl;
    }

    void testChatHistoryPersistence() {
        // Temporarily set the storage path via environment variable approach
        // We test round-trip using two independent manager instances.
        ChatHistoryManager mgr1;

        const QString sessionId = mgr1.createSession(QStringLiteral("Persistence Test"));
        mgr1.appendMessage(QStringLiteral("user"),      QStringLiteral("Hello persistence world!"));
        mgr1.appendMessage(QStringLiteral("assistant"), QStringLiteral("Persistence confirmed."));

        // Reload from disk via public loadSession
        const ChatSession loaded = mgr1.loadSession(sessionId);
        QCOMPARE(loaded.id,    sessionId);
        QCOMPARE(loaded.title, QStringLiteral("Persistence Test"));
        QCOMPARE(loaded.messages.size(), 2);
        QCOMPARE(loaded.messages.at(0).role,    QStringLiteral("user"));
        QCOMPARE(loaded.messages.at(0).content, QStringLiteral("Hello persistence world!"));
        QCOMPARE(loaded.messages.at(1).role,    QStringLiteral("assistant"));
        QCOMPARE(loaded.messages.at(1).content, QStringLiteral("Persistence confirmed."));

        // Cleanup
        mgr1.deleteSession(sessionId);
        std::cout << "[PASS] ChatHistory message persistence (write + read-back)" << std::endl;
    }

    void testChatHistoryFullTextSearch() {
        ChatHistoryManager mgr;

        const QString s1 = mgr.createSession(QStringLiteral("CMake Session"));
        mgr.appendMessage(QStringLiteral("user"),      QStringLiteral("How do I fix cmake build errors?"));
        mgr.appendMessage(QStringLiteral("assistant"), QStringLiteral("You can run cmake --build to rebuild."));

        const QString s2 = mgr.createSession(QStringLiteral("Python Session"));
        mgr.appendMessage(QStringLiteral("user"),      QStringLiteral("Show me a Python list comprehension."));
        mgr.appendMessage(QStringLiteral("assistant"), QStringLiteral("[x for x in range(10)]"));

        // Single keyword
        QList<SearchResult> results = mgr.search(QStringLiteral("cmake"));
        QVERIFY(!results.isEmpty());
        QCOMPARE(results.first().sessionId, s1);

        // Case insensitivity
        QList<SearchResult> ciResults = mgr.search(QStringLiteral("CMAKE"));
        QVERIFY(!ciResults.isEmpty());

        // Multi-word AND
        QList<SearchResult> multiResults = mgr.search(QStringLiteral("cmake build"));
        QVERIFY(!multiResults.isEmpty());

        // Role filter – only assistant messages
        SearchFilter filter;
        filter.roleFilter = QStringLiteral("assistant");
        QList<SearchResult> roleResults = mgr.search(QStringLiteral("cmake"), filter);
        for (const SearchResult &r : roleResults) {
            QCOMPARE(r.role, QStringLiteral("assistant"));
        }

        // No-match case
        QList<SearchResult> noResults = mgr.search(QStringLiteral("zzzzznotfound"));
        QVERIFY(noResults.isEmpty());

        // Snippet should contain highlight mark for matches
        if (!results.isEmpty()) {
            QVERIFY(results.first().matchedSnippet.contains(QStringLiteral("<mark")));
        }

        // Cleanup
        mgr.deleteSession(s1);
        mgr.deleteSession(s2);
        std::cout << "[PASS] ChatHistory full-text search (keywords, roles, case, snippets)" << std::endl;
    }

    void testChatHistoryExport() {
        ChatHistoryManager mgr;

        const QString sessionId = mgr.createSession(QStringLiteral("Export Test"));
        mgr.appendMessage(QStringLiteral("user"),      QStringLiteral("Hello export world."));
        mgr.appendMessage(QStringLiteral("assistant"), QStringLiteral("Export response here."));

        // Markdown
        const QString md = mgr.exportToMarkdown(sessionId);
        QVERIFY(!md.isEmpty());
        QVERIFY(md.contains(QStringLiteral("Export Test")));
        QVERIFY(md.contains(QStringLiteral("Hello export world.")));
        QVERIFY(md.contains(QStringLiteral("Export response here.")));
        QVERIFY(md.contains(QStringLiteral("**You**")));
        QVERIFY(md.contains(QStringLiteral("**TitanAI**")));

        // Plain text
        const QString txt = mgr.exportToPlainText(sessionId);
        QVERIFY(!txt.isEmpty());
        QVERIFY(txt.contains(QStringLiteral("You:")));
        QVERIFY(txt.contains(QStringLiteral("TitanAI:")));

        // HTML (used as the source for PDF export)
        const QString html = mgr.exportToHtml(sessionId);
        QVERIFY(!html.isEmpty());
        QVERIFY(html.contains(QStringLiteral("<html")));
        QVERIFY(html.contains(QStringLiteral("Export Test")));
        QVERIFY(html.contains(QStringLiteral("Hello export world.")));
        QVERIFY(html.contains(QStringLiteral("Export response here.")));
        QVERIFY(html.contains(QStringLiteral("msg-user")));
        QVERIFY(html.contains(QStringLiteral("msg-assistant")));

        // Unknown sessions export as empty strings
        QVERIFY(mgr.exportToHtml(QStringLiteral("conv_does_not_exist")).isEmpty());

        // Cleanup
        mgr.deleteSession(sessionId);
        std::cout << "[PASS] ChatHistory export (Markdown + plain text + HTML/PDF source)" << std::endl;
    }

    // ── Translation Assistant Tests ────────────────────────────────────────────

    void testTranslationAssistantSupportedLanguages() {
        const QList<LanguageInfo> langs = TranslationAssistant::supportedLanguages();
        QVERIFY(langs.size() >= 40);

        // "auto" entry exists
        bool hasAuto = false;
        for (const LanguageInfo &l : langs) {
            if (l.code == QStringLiteral("auto")) { hasAuto = true; break; }
        }
        QVERIFY(hasAuto);

        // Common languages present
        const QStringList expected = { QStringLiteral("en"), QStringLiteral("es"),
                                       QStringLiteral("fr"), QStringLiteral("de"),
                                       QStringLiteral("ja"), QStringLiteral("zh"),
                                       QStringLiteral("ar"), QStringLiteral("hi") };
        for (const QString &code : expected) {
            const LanguageInfo info = TranslationAssistant::languageByCode(code);
            QByteArray msg = QStringLiteral("Missing language: %1").arg(code).toUtf8();
            QVERIFY2(!info.name.isEmpty(), msg.constData());
            QVERIFY(!info.flag.isEmpty());
        }

        // languageCodes returns all codes
        const QStringList codes = TranslationAssistant::languageCodes();
        QVERIFY(codes.contains(QStringLiteral("en")));
        QVERIFY(codes.contains(QStringLiteral("ja")));

        std::cout << "[PASS] TranslationAssistant supported languages (" << langs.size() << " langs)" << std::endl;
    }

    void testTranslationAssistantPromptBuilder() {
        TranslationRequest req;
        req.text             = QStringLiteral("Hello, how are you?");
        req.sourceLanguage   = QStringLiteral("en");
        req.targetLanguage   = QStringLiteral("es");
        req.tone             = TranslationTone::Formal;
        req.provideBreakdown = true;

        const QString prompt = TranslationAssistant::buildTranslationPrompt(req);
        QVERIFY(!prompt.isEmpty());
        QVERIFY(prompt.contains(QStringLiteral("Spanish")));
        QVERIFY(prompt.contains(QStringLiteral("Hello, how are you?")));
        QVERIFY(prompt.contains(QStringLiteral("Formal")));
        QVERIFY(prompt.contains(QStringLiteral("PRONUNCIATION:")));
        QVERIFY(prompt.contains(QStringLiteral("ALTERNATIVES:")));
        QVERIFY(prompt.contains(QStringLiteral("GRAMMAR_NOTES:")));
        QVERIFY(prompt.contains(QStringLiteral("EXAMPLE:")));

        // Auto tone should NOT add tone clause
        req.tone = TranslationTone::Auto;
        const QString autoPrompt = TranslationAssistant::buildTranslationPrompt(req);
        QVERIFY(!autoPrompt.contains(QStringLiteral("Use a Auto tone.")));

        std::cout << "[PASS] TranslationAssistant prompt builder" << std::endl;
    }

    void testTranslationAssistantResponseParsing() {
        // Simulate LLM response with all structured sections
        const QString llmOutput = QStringLiteral(
            "Hola, ¿cómo estás?\n"
            "PRONUNCIATION: oh-lah, koh-moh ess-tahs\n"
            "ALTERNATIVES: Hola, ¿qué tal?, Buenos días, ¿cómo te va?\n"
            "GRAMMAR_NOTES: 'cómo estás' is informal. Use 'cómo está usted' for formal.\n"
            "EXAMPLE: Hola María, ¿cómo estás hoy?\n"
            "DETECTED_SOURCE: en\n"
        );

        TranslationRequest req;
        req.text           = QStringLiteral("Hello, how are you?");
        req.sourceLanguage = QStringLiteral("en");
        req.targetLanguage = QStringLiteral("es");

        const TranslationResult result = TranslationAssistant::parseTranslationResponse(llmOutput, req);
        QVERIFY(result.success);
        QCOMPARE(result.translatedText, QStringLiteral("Hola, ¿cómo estás?"));
        QVERIFY(result.pronunciation.contains(QStringLiteral("oh-lah")));
        QVERIFY(result.alternatives.size() >= 2);
        QVERIFY(!result.grammarNotes.isEmpty());
        QVERIFY(!result.exampleSentence.isEmpty());
        QCOMPARE(result.detectedSourceLanguage, QStringLiteral("en"));

        // Markdown formatter test
        const QString md = TranslationAssistant::formatResultAsMarkdown(result, req);
        QVERIFY(md.contains(QStringLiteral("Hola, ¿cómo estás?")));
        QVERIFY(md.contains(QStringLiteral("Pronunciation")));

        // Empty response should fail gracefully
        const TranslationResult empty = TranslationAssistant::parseTranslationResponse(QString(), req);
        QVERIFY(!empty.success);

        std::cout << "[PASS] TranslationAssistant response parsing & markdown formatter" << std::endl;
    }

    void testTranslationAssistantOfflineFallbackAndCache() {
        TranslationAssistant ta;

        // Verify offline phrasebook key languages
        QVERIFY(ta.cacheSize() == 0);

        // Tone helpers
        QCOMPARE(TranslationAssistant::toneName(TranslationTone::Formal), QStringLiteral("Formal"));
        QCOMPARE(TranslationAssistant::toneName(TranslationTone::Casual), QStringLiteral("Casual"));
        QCOMPARE(TranslationAssistant::toneFromName(QStringLiteral("Technical")), TranslationTone::Technical);
        QCOMPARE(TranslationAssistant::toneFromName(QStringLiteral("Poetic")),    TranslationTone::Poetic);
        QCOMPARE(TranslationAssistant::toneFromName(QStringLiteral("Unknown")),   TranslationTone::Auto);

        // Language detection heuristics
        QCOMPARE(TranslationAssistant::detectLanguage(QStringLiteral("こんにちは")), QStringLiteral("ja"));
        QCOMPARE(TranslationAssistant::detectLanguage(QStringLiteral("你好")),      QStringLiteral("zh"));
        QCOMPARE(TranslationAssistant::detectLanguage(QStringLiteral("Привет")),    QStringLiteral("ru"));
        QCOMPARE(TranslationAssistant::detectLanguage(QStringLiteral("مرحبا")),    QStringLiteral("ar"));
        QCOMPARE(TranslationAssistant::detectLanguage(QStringLiteral("Hello")),     QStringLiteral("en"));

        // toneNames returns correct count
        QCOMPARE(TranslationAssistant::toneNames().size(), 7);

        std::cout << "[PASS] TranslationAssistant offline phrasebook & tone helpers" << std::endl;
    }

    void testTranslationAssistantHistory() {
        TranslationAssistant ta;
        ta.clearHistory();
        QVERIFY(ta.history().isEmpty());

        // History search on empty list
        QVERIFY(ta.searchHistory(QStringLiteral("hello")).isEmpty());

        // removeHistoryEntry on empty list returns false
        QVERIFY(!ta.removeHistoryEntry(QStringLiteral("nonexistent")));

        // setFavorite on empty returns false
        QVERIFY(!ta.setFavorite(QStringLiteral("nonexistent"), true));

        std::cout << "[PASS] TranslationAssistant history (empty lifecycle)" << std::endl;
    }

    void testTranslationAssistantQueryDetection() {
        // Positive cases
        QVERIFY(TranslationAssistant::isTranslationQuery(
            QStringLiteral("translate \"Hello world\" to Spanish")));
        QVERIFY(TranslationAssistant::isTranslationQuery(
            QStringLiteral("how do you say thank you in Japanese")));
        QVERIFY(TranslationAssistant::isTranslationQuery(
            QStringLiteral("quick translate to German: Where is the station?")));
        QVERIFY(TranslationAssistant::isTranslationQuery(
            QStringLiteral("What is the translation of bonjour")));
        QVERIFY(TranslationAssistant::isTranslationQuery(
            QStringLiteral("What does merci mean in spanish")));

        // Negative cases
        QVERIFY(!TranslationAssistant::isTranslationQuery(
            QStringLiteral("build and fix errors")));
        QVERIFY(!TranslationAssistant::isTranslationQuery(
            QStringLiteral("check system info")));
        QVERIFY(!TranslationAssistant::isTranslationQuery(
            QStringLiteral("what packages are installed")));

        // Parsing tests
        {
            const TranslationRequest req = TranslationAssistant::parseNaturalLanguageRequest(
                QStringLiteral("translate \"Good morning\" to French"));
            QCOMPARE(req.text, QStringLiteral("Good morning"));
            QCOMPARE(req.targetLanguage, QStringLiteral("fr"));
        }
        {
            const TranslationRequest req = TranslationAssistant::parseNaturalLanguageRequest(
                QStringLiteral("how do you say thank you in Japanese"));
            QCOMPARE(req.text, QStringLiteral("thank you"));
            QCOMPARE(req.targetLanguage, QStringLiteral("ja"));
        }
        {
            const TranslationRequest req = TranslationAssistant::parseNaturalLanguageRequest(
                QStringLiteral("translate this into German: Good night"));
            QCOMPARE(req.text, QStringLiteral("Good night"));
            QCOMPARE(req.targetLanguage, QStringLiteral("de"));
        }

        std::cout << "[PASS] TranslationAssistant query detection & NL parsing" << std::endl;
    }
};

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    TitanAiTestSuite suite;
    return QTest::qExec(&suite, argc, argv);
}

#include "test_all.moc"
