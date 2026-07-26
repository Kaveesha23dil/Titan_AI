#include <QCoreApplication>
#include <QThread>
#include <QSemaphore>
#include <iostream>
#include <string>

#include "llm/ollama_client.hpp"

class InputReaderThread : public QThread {
    Q_OBJECT
public:
    explicit InputReaderThread(QObject *parent = nullptr) : QThread(parent) {}
    ~InputReaderThread() override {
        requestInterruption();
        m_semaphore.release();
        wait();
    }

    void requestNextLine() {
        m_semaphore.release();
    }

signals:
    void lineRead(const QString &line);
    void inputClosed();

protected:
    void run() override {
        while (!isInterruptionRequested()) {
            m_semaphore.acquire();
            if (isInterruptionRequested()) {
                break;
            }

            std::string line;
            if (!std::getline(std::cin, line)) {
                emit inputClosed();
                break;
            }
            emit lineRead(QString::fromStdString(line));
        }
    }

private:
    QSemaphore m_semaphore;
};

#include "main.moc"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    std::cout << "TitanAI v0.2.0\n";
    std::cout << "Lightweight Arch Linux AI Assistant\n";
    std::cout << "------------------------------------\n\n";
    std::cout << "Type 'exit' or 'quit' to close TitanAI.\n\n";

    OllamaClient client;
    InputReaderThread inputThread;
    bool isWaitingForResponse = false;
    QString pendingInput;
    bool hasPendingInput = false;

    auto promptAndRead = [&]() {
        std::cout << "You: " << std::flush;
        inputThread.requestNextLine();
    };

    auto processInput = [&](const QString &inputStr) {
        if (inputStr.compare(QStringLiteral("exit"), Qt::CaseInsensitive) == 0 ||
            inputStr.compare(QStringLiteral("quit"), Qt::CaseInsensitive) == 0) {
            std::cout << "\nGoodbye!\n" << std::flush;
            QCoreApplication::quit();
            return;
        }

        if (inputStr.isEmpty()) {
            promptAndRead();
            return;
        }

        isWaitingForResponse = true;
        std::cout << "\nTitanAI:\n" << std::flush;
        client.sendPrompt(inputStr);
    };

    QObject::connect(&inputThread, &InputReaderThread::lineRead, &app, [&](const QString &line) {
        QString inputStr = line.trimmed();

        if (isWaitingForResponse) {
            // Queue input for processing after current response completes
            pendingInput = inputStr;
            hasPendingInput = true;
            return;
        }

        processInput(inputStr);
    }, Qt::QueuedConnection);

    QObject::connect(&inputThread, &InputReaderThread::inputClosed, &app, [&]() {
        std::cout << "\nGoodbye!\n" << std::flush;
        QCoreApplication::quit();
    }, Qt::QueuedConnection);

    auto onResponseComplete = [&]() {
        isWaitingForResponse = false;

        if (hasPendingInput) {
            hasPendingInput = false;
            QString input = pendingInput;
            pendingInput.clear();
            processInput(input);
        } else {
            promptAndRead();
        }
    };

    QObject::connect(&client, &OllamaClient::responseReceived, &app, [&](const QString &response) {
        std::cout << response.toStdString() << "\n\n" << std::flush;
        onResponseComplete();
    }, Qt::QueuedConnection);

    QObject::connect(&client, &OllamaClient::errorOccurred, &app, [&](const QString &error) {
        std::cerr << "Error: " << error.toStdString() << "\n\n" << std::flush;
        onResponseComplete();
    }, Qt::QueuedConnection);

    promptAndRead();
    inputThread.start();

    return app.exec();
}
