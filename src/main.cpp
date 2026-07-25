#include <QCoreApplication>
#include <iostream>

#include "llm/ollama_client.hpp"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    std::cout << "TitanAI v0.1.0\n";
    std::cout << "Lightweight Arch Linux AI Assistant\n";
    std::cout << "------------------------------------\n";
    std::cout << "Sending test prompt to Ollama (gemma3:4b)...\n\n";

    OllamaClient client;

    QObject::connect(&client, &OllamaClient::responseReceived, [](const QString &response) {
        std::cout << "--- Ollama AI Response ---\n";
        std::cout << response.toStdString() << "\n";
        std::cout << "--------------------------\n";
        QCoreApplication::quit();
    });

    QObject::connect(&client, &OllamaClient::errorOccurred, [](const QString &error) {
        std::cerr << "Error encountered: " << error.toStdString() << "\n";
        QCoreApplication::exit(1);
    });

    client.sendPrompt(QStringLiteral("Hello TitanAI. Introduce yourself as a lightweight AI assistant designed for Arch Linux users."));

    return app.exec();
}
