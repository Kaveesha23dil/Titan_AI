#include "llm/ollama_manager.hpp"

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
        for (const QJsonValue &value : models) {
            if (value.toObject().value(QStringLiteral("name")).toString() == m_model) {
                m_pollTimer.stop();
                emit statusChanged(Status::Ready, QStringLiteral("Model ready: %1").arg(m_model));
                emit modelReady(m_model);
                return;
            }
        }

        m_pollTimer.stop();
        failWithError(QStringLiteral("Model '%1' is not installed. Install it with: ollama pull %1")
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
