#include "agent/agent.hpp"

Agent::Agent(QObject *parent)
    : QObject(parent)
    , m_ollamaClient(this)
{
    connect(&m_ollamaClient, &OllamaClient::responseReceived, this, &Agent::responseReceived);
    connect(&m_ollamaClient, &OllamaClient::errorOccurred, this, &Agent::errorOccurred);
}

void Agent::sendMessage(const QString &message)
{
    m_ollamaClient.sendPrompt(message);
}
