#ifndef TITANAI_AGENT_HPP
#define TITANAI_AGENT_HPP

#include <QObject>
#include <QString>
#include "llm/ollama_client.hpp"
#include "llm/ollama_manager.hpp"
#include "tools/system_info.hpp"

class Agent : public QObject {
    Q_OBJECT

public:
    static constexpr const char *kDefaultModel = "gemma3:4b";

    explicit Agent(QObject *parent = nullptr);
    ~Agent() override = default;

    void sendMessage(const QString &message);
    void initializeModel(const QString &model);

signals:
    void responseChunkReceived(const QString &chunk);
    void responseReceived(const QString &response);
    void errorOccurred(const QString &error);
    void modelStatusChanged(OllamaManager::Status status, const QString &message);
    void modelReady(const QString &model);
    void modelError(const QString &error);

private slots:
    void onModelReady(const QString &model);

private:
    bool handleSystemInfoQuery(const QString &message);

    OllamaManager m_ollamaManager;
    OllamaClient m_ollamaClient;
    SystemInfoTool m_systemInfoTool;
};

#endif // TITANAI_AGENT_HPP
