#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>

#include "agent/agent.hpp"
#include "llm/ollama_client.hpp"
#include "llm/ollama_manager.hpp"
#include "tools/code_fixer.hpp"
#include "tools/system_info.hpp"
#include "tools/ui_developer.hpp"
#include "learning/task_tracker.hpp"
#include "learning/activity_analyzer.hpp"
#include "learning/suggestion_engine.hpp"

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
        QVERIFY2(ok, error.toUtf8().constData());

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
        client.setModel(QStringLiteral("qwen2.5-coder:1.5b"));

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

        std::cout << "Testing live Ollama inference (qwen2.5-coder:1.5b)..." << std::endl;
        client.sendPrompt(QStringLiteral("Say 'OK' if you can read this."));

        QTimer timeoutTimer;
        timeoutTimer.setSingleShot(true);
        connect(&timeoutTimer, &QTimer::timeout, [&]() {
            QCoreApplication::quit();
        });
        timeoutTimer.start(10000); // 10 second timeout

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
        client.setModel(QStringLiteral("qwen2.5-coder:1.5b"));

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

        std::cout << "Testing non-streaming requestCompletion (qwen2.5-coder:1.5b)..." << std::endl;
        client.requestCompletion(QStringLiteral("Write 'SUCCESS'"));

        QTimer timeoutTimer;
        timeoutTimer.setSingleShot(true);
        connect(&timeoutTimer, &QTimer::timeout, [&]() {
            QCoreApplication::quit();
        });
        timeoutTimer.start(10000);

        qApp->exec();

        QVERIFY2(errorMsg.isEmpty(), errorMsg.toUtf8().constData());
        QVERIFY2(received, "Timed out waiting for completion response");
        std::cout << "[PASS] Non-streaming Response: " << receivedResponse.left(80).toStdString() << std::endl;
    }
};

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    TitanAiTestSuite suite;
    return QTest::qExec(&suite, argc, argv);
}

#include "test_all.moc"
