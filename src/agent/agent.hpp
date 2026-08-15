#ifndef TITANAI_AGENT_HPP
#define TITANAI_AGENT_HPP

#include <QObject>
#include <QString>
#include <QStringList>
#include "llm/ollama_client.hpp"
#include "llm/ollama_manager.hpp"
#include "tools/package_manager.hpp"
#include "tools/system_info.hpp"

class Agent : public QObject {
    Q_OBJECT

public:
    static constexpr const char *kDefaultModel = "gemma3:4b";

    explicit Agent(QObject *parent = nullptr);
    ~Agent() override = default;

    void sendMessage(const QString &message);
    void initializeModel(const QString &model);
    void performInstall(const QStringList &packages);

signals:
    void responseChunkReceived(const QString &chunk);
    void responseReceived(const QString &response);
    void errorOccurred(const QString &error);
    void modelStatusChanged(OllamaManager::Status status, const QString &message);
    void modelReady(const QString &model);
    void modelError(const QString &error);
    void installRequested(const QStringList &packages);
    void toolOutputReceived(const QString &line);

private slots:
    void onModelReady(const QString &model);
    void onPackageManagerFinished(bool success, const QString &summary);

private:
    bool handleSystemInfoQuery(const QString &message);
    bool handlePackageInstallQuery(const QString &message);
    QStringList extractPackageNames(const QString &message) const;
    static bool isInformationalQuery(const QString &lowerMessage);

    OllamaManager m_ollamaManager;
    OllamaClient m_ollamaClient;
    SystemInfoTool m_systemInfoTool;
    PackageManager m_packageManager;
};

#endif // TITANAI_AGENT_HPP
