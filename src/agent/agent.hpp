#ifndef TITANAI_AGENT_HPP
#define TITANAI_AGENT_HPP

#include <QObject>
#include <QString>
#include "llm/ollama_client.hpp"

class Agent : public QObject {
    Q_OBJECT

public:
    explicit Agent(QObject *parent = nullptr);
    ~Agent() override = default;

    void sendMessage(const QString &message);

signals:
    void responseReceived(const QString &response);
    void errorOccurred(const QString &error);

private:
    OllamaClient m_ollamaClient;
};

#endif // TITANAI_AGENT_HPP
