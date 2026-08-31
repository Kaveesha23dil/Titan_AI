#include "llm/ollama_manager.hpp"

#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>

OllamaManager::OllamaManager(QObject *parent)
    : QObject(parent)
{
    m_pollTimer.setInterval(kPollIntervalMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &OllamaManager::checkServerAndModel);
}

OllamaManager::~OllamaManager()
{
    m_pollTimer.stop();

    if (m_startedByUs && m_serverProcess && m_serverProcess->state() != QProcess::NotRunning) {
        m_serverProcess->terminate();
        if (!m_serverProcess->waitForFinished(3000)) {
            m_serverProcess->kill();
        }
    }

    delete m_serverProcess;
}

void OllamaManager::ensureModelReady(const QString &model)
{
    m_model = model;
    m_attempts = 0;

    emit statusChanged(Status::CheckingServer, QStringLiteral("Checking Ollama server..."));
    m_pollTimer.start();
    checkServerAndModel();
}

bool OllamaManager::isVisionCapable(const QString &modelName)
{
    const QString lower = modelName.toLower();
    return lower.contains(QStringLiteral("gemma3")) ||
           lower.contains(QStringLiteral("llava")) ||
           lower.contains(QStringLiteral("vision")) ||
           lower.contains(QStringLiteral("minicpm-v")) ||
           lower.contains(QStringLiteral("vl")) ||
           lower.contains(QStringLiteral("bakllava"));
}

// "org/model:tag" -> "model:tag" (strips optional namespace prefix)
QString OllamaManager::parseInstalled(const QString &fullName) const
{
    QString name = fullName;
    const int slash = name.lastIndexOf(QLatin1Char('/'));
    if (slash >= 0) {
        name = name.mid(slash + 1);
    }
    const int at = name.indexOf(QLatin1Char('@'));
    if (at >= 0) {
        name = name.left(at);
    }
    return name;
}

void OllamaManager::refreshModels()
{
    if (m_startedByUs && m_serverProcess && m_serverProcess->state() == QProcess::NotRunning) {
        return;
    }

    QNetworkRequest request(m_tagsUrl);
    request.setTransferTimeout(2500);

    QNetworkReply *reply = m_networkManager.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        handleTagsReply(reply);
    });
}

void OllamaManager::handleTagsReply(QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        emit modelsChanged(m_installedModels);
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        emit modelsChanged(m_installedModels);
        return;
    }

    QJsonArray models = doc.object().value(QStringLiteral("models")).toArray();

    QStringList installed;
    installed.reserve(static_cast<int>(models.size()));
    for (const QJsonValue &value : models) {
        const QString name = parseInstalled(value.toObject().value(QStringLiteral("name")).toString());
        if (!name.isEmpty()) {
            installed.append(name);
        }
    }
    installed.removeDuplicates();
    std::sort(installed.begin(), installed.end(), [](const QString &a, const QString &b) {
        return QString::compare(a, b, Qt::CaseInsensitive) < 0;
    });

    m_installedModels = installed;
    emit modelsChanged(m_installedModels);
}

void OllamaManager::checkServerAndModel()
{
    if (m_attempts++ >= kMaxAttempts) {
        failWithError(QStringLiteral("Ollama server did not become ready in time. Is the 'ollama' "
                                     "package installed? Try running 'ollama serve' manually."));
        return;
    }

    QNetworkRequest request(m_tagsUrl);
    request.setTransferTimeout(2000);

    QNetworkReply *reply = m_networkManager.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            if (!m_startedByUs && m_serverProcess == nullptr) {
                startServer();
            }
            return;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            return;
        }

        QJsonArray models = doc.object().value(QStringLiteral("models")).toArray();
        QString selectedModel;
        m_visionModel.clear();
        m_installedModels.clear();

        QStringList installedNames;
        installedNames.reserve(static_cast<int>(models.size()));

        // Detect any installed vision-capable models (e.g. gemma3:4b, llava)
        for (const QJsonValue &value : models) {
            const QString name = parseInstalled(value.toObject().value(QStringLiteral("name")).toString());
            installedNames.append(name);
            if (isVisionCapable(name)) {
                m_visionModel = name;
                break;
            }
        }

        installedNames.removeDuplicates();
        std::sort(installedNames.begin(), installedNames.end(), [](const QString &a, const QString &b) {
            return QString::compare(a, b, Qt::CaseInsensitive) < 0;
        });
        m_installedModels = installedNames;

        // 1. Check for requested model (exact or prefix match)
        for (const QString &name : installedNames) {
            if (name == m_model ||
                name.startsWith(m_model + QStringLiteral(":")) ||
                (m_model.contains(QLatin1Char(':')) && name == m_model.section(QLatin1Char(':'), 0, 0)) ||
                (name.contains(QLatin1Char(':')) && name.section(QLatin1Char(':'), 0, 0) == m_model)) {
                selectedModel = name;
                break;
            }
        }

        // 2. Fallback to any installed coder model, or any available model
        if (selectedModel.isEmpty() && !installedNames.isEmpty()) {
            for (const QString &name : installedNames) {
                if (name.contains(QStringLiteral("coder"), Qt::CaseInsensitive) ||
                    name.contains(QStringLiteral("qwen"), Qt::CaseInsensitive)) {
                    selectedModel = name;
                    break;
                }
            }
            if (selectedModel.isEmpty()) {
                selectedModel = installedNames.first();
            }
        }

        if (!selectedModel.isEmpty()) {
            m_pollTimer.stop();
            emit statusChanged(Status::Ready, QStringLiteral("Model ready: %1").arg(selectedModel));
            emit modelReady(selectedModel);
            emit modelsChanged(m_installedModels);
            return;
        }

        m_pollTimer.stop();
        emit modelsChanged(m_installedModels);
        failWithError(QStringLiteral("No models installed in Ollama. Install one with: ollama pull %1")
                          .arg(m_model));
    });
}

void OllamaManager::startServer()
{
    m_startedByUs = true;
    m_serverProcess = new QProcess(this);
    m_serverProcess->setProgram(QStringLiteral("ollama"));
    m_serverProcess->setArguments({QStringLiteral("serve")});

    connect(m_serverProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_pollTimer.stop();
            failWithError(QStringLiteral("Failed to start Ollama server. Is the 'ollama' package installed?"));
        }
    });

    m_serverProcess->start(QIODevice::ReadOnly);

    emit statusChanged(Status::StartingServer, QStringLiteral("Starting local AI model server..."));
}

void OllamaManager::failWithError(const QString &error)
{
    m_pollTimer.stop();
    emit statusChanged(Status::Error, error);
    emit modelError(error);
}
