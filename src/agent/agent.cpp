#include "agent/agent.hpp"

#include <QTimer>

Agent::Agent(QObject *parent)
    : QObject(parent)
    , m_ollamaClient(this)
{
    connect(&m_ollamaClient, &OllamaClient::responseChunkReceived, this, &Agent::responseChunkReceived);
    connect(&m_ollamaClient, &OllamaClient::responseReceived, this, &Agent::responseReceived);
    connect(&m_ollamaClient, &OllamaClient::errorOccurred, this, &Agent::errorOccurred);

    connect(&m_ollamaManager, &OllamaManager::statusChanged, this, &Agent::modelStatusChanged);
    connect(&m_ollamaManager, &OllamaManager::modelReady, this, &Agent::onModelReady);
    connect(&m_ollamaManager, &OllamaManager::modelError, this, &Agent::modelError);
}

void Agent::initializeModel(const QString &model)
{
    m_ollamaManager.ensureModelReady(model);
}

void Agent::onModelReady(const QString &model)
{
    m_ollamaClient.setModel(model);
    m_ollamaClient.warmUp();
    emit modelReady(model);
}

void Agent::sendMessage(const QString &message)
{
    if (handleSystemInfoQuery(message)) {
        return;
    }

    m_ollamaClient.sendPrompt(message);
}

bool Agent::handleSystemInfoQuery(const QString &message)
{
    QString lower = message.toLower().trimmed();
    SystemInfo info = m_systemInfoTool.getSystemInfo();

    QString response;

    bool isOsQuery = lower.contains(QStringLiteral("operating system")) ||
                     lower.contains(QStringLiteral("linux distribution")) ||
                     lower.contains(QStringLiteral("distro")) ||
                     lower.contains(QStringLiteral("os am i running")) ||
                     lower.contains(QStringLiteral("what os")) ||
                     lower.contains(QStringLiteral("which os"));

    bool isKernelQuery = lower.contains(QStringLiteral("kernel"));

    bool isCpuQuery = lower.contains(QStringLiteral("cpu")) ||
                      lower.contains(QStringLiteral("processor"));

    bool isAvailRamQuery = lower.contains(QStringLiteral("available")) ||
                           lower.contains(QStringLiteral("free"));

    bool isRamQuery = lower.contains(QStringLiteral("ram")) ||
                      lower.contains(QStringLiteral("memory"));

    bool isGeneralSysQuery = lower.contains(QStringLiteral("system info")) ||
                             lower.contains(QStringLiteral("system specs")) ||
                             lower.contains(QStringLiteral("my system")) ||
                             lower.contains(QStringLiteral("sysinfo")) ||
                             lower.contains(QStringLiteral("system summary"));

    if (isGeneralSysQuery) {
        double totalGB = static_cast<double>(info.totalMemoryBytes) / (1024.0 * 1024.0 * 1024.0);
        double availGB = static_cast<double>(info.availableMemoryBytes) / (1024.0 * 1024.0 * 1024.0);

        response = QStringLiteral("System Information:\n") +
                   QStringLiteral("  Operating System: %1\n").arg(info.operatingSystem) +
                   QStringLiteral("  Kernel: %1 (%2)\n").arg(info.kernelVersion, info.architecture) +
                   QStringLiteral("  Hostname: %1\n").arg(info.hostname) +
                   QStringLiteral("  CPU: %1 (%2 cores)\n").arg(info.cpuModel).arg(info.cpuCoreCount) +
                   QStringLiteral("  Total Memory: %1 GB\n").arg(QString::number(totalGB, 'f', 2)) +
                   QStringLiteral("  Available Memory: %1 GB").arg(QString::number(availGB, 'f', 2));
    } else if (isOsQuery) {
        response = QStringLiteral("Operating System: %1").arg(info.operatingSystem);
    } else if (isKernelQuery) {
        response = QStringLiteral("Kernel Version: %1 (%2)").arg(info.kernelVersion, info.architecture);
    } else if (isCpuQuery) {
        response = QStringLiteral("CPU: %1 (%2 cores)").arg(info.cpuModel).arg(info.cpuCoreCount);
    } else if (isRamQuery && isAvailRamQuery) {
        double availGB = static_cast<double>(info.availableMemoryBytes) / (1024.0 * 1024.0 * 1024.0);
        double availMB = static_cast<double>(info.availableMemoryBytes) / (1024.0 * 1024.0);
        response = QStringLiteral("Available RAM: %1 GB (%2 MB)").arg(QString::number(availGB, 'f', 2), QString::number(availMB, 'f', 0));
    } else if (isRamQuery) {
        double totalGB = static_cast<double>(info.totalMemoryBytes) / (1024.0 * 1024.0 * 1024.0);
        double totalMB = static_cast<double>(info.totalMemoryBytes) / (1024.0 * 1024.0);
        response = QStringLiteral("Total RAM: %1 GB (%2 MB)").arg(QString::number(totalGB, 'f', 2), QString::number(totalMB, 'f', 0));
    } else {
        return false;
    }

    QTimer::singleShot(0, this, [this, response]() {
        emit responseReceived(response);
    });

    return true;
}
