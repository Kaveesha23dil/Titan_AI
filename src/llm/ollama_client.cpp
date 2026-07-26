#include "llm/ollama_client.hpp"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>

OllamaClient::OllamaClient(QObject *parent)
    : QObject(parent)
{
}

void OllamaClient::clearHistory()
{
    m_history = QJsonArray();
}

const QJsonArray& OllamaClient::history() const
{
    return m_history;
}

void OllamaClient::sendPrompt(const QString &prompt, const QString &model)
{
    QString trimmedPrompt = prompt.trimmed();
    if (trimmedPrompt.isEmpty()) {
        return;
    }

    QJsonObject userMessageObj;
    userMessageObj[QStringLiteral("role")] = QStringLiteral("user");
    userMessageObj[QStringLiteral("content")] = trimmedPrompt;

    m_history.append(userMessageObj);

    QNetworkRequest request(m_endpointUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject payloadObj;
    payloadObj[QStringLiteral("model")] = model;
    payloadObj[QStringLiteral("messages")] = m_history;
    payloadObj[QStringLiteral("stream")] = false;

    QJsonDocument payloadDoc(payloadObj);
    QByteArray postData = payloadDoc.toJson(QJsonDocument::Compact);

    QNetworkReply *reply = m_networkManager.post(request, postData);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray responseData = reply->readAll();
        QNetworkReply::NetworkError netError = reply->error();

        if (netError != QNetworkReply::NoError || statusCode >= 400) {
            if (!m_history.isEmpty()) {
                m_history.removeAt(m_history.size() - 1);
            }

            QString errorDetails;
            if (statusCode > 0) {
                errorDetails += QStringLiteral("HTTP Status %1. ").arg(statusCode);
            }
            if (netError != QNetworkReply::NoError) {
                errorDetails += QStringLiteral("Network Error (%1): %2. ").arg(netError).arg(reply->errorString());
            }

            QJsonParseError parseError;
            QJsonDocument errDoc = QJsonDocument::fromJson(responseData, &parseError);
            if (parseError.error == QJsonParseError::NoError && errDoc.isObject() && errDoc.object().contains(QStringLiteral("error"))) {
                errorDetails += QStringLiteral("Ollama Error: %1").arg(errDoc.object()[QStringLiteral("error")].toString());
            } else if (!responseData.isEmpty()) {
                errorDetails += QStringLiteral("Response Body: %1").arg(QString::fromUtf8(responseData));
            }

            emit errorOccurred(errorDetails);
            return;
        }

        QJsonParseError parseError;
        QJsonDocument responseDoc = QJsonDocument::fromJson(responseData, &parseError);

        if (parseError.error != QJsonParseError::NoError || !responseDoc.isObject()) {
            if (!m_history.isEmpty()) {
                m_history.removeAt(m_history.size() - 1);
            }
            QString errorMsg = QStringLiteral("Invalid JSON response from Ollama server: %1").arg(parseError.errorString());
            emit errorOccurred(errorMsg);
            return;
        }

        QJsonObject rootObj = responseDoc.object();

        if (rootObj.contains(QStringLiteral("error"))) {
            if (!m_history.isEmpty()) {
                m_history.removeAt(m_history.size() - 1);
            }
            QString errorMsg = QStringLiteral("Ollama API Error: %1").arg(rootObj[QStringLiteral("error")].toString());
            emit errorOccurred(errorMsg);
            return;
        }

        if (!rootObj.contains(QStringLiteral("message"))) {
            if (!m_history.isEmpty()) {
                m_history.removeAt(m_history.size() - 1);
            }
            emit errorOccurred(QStringLiteral("Invalid Response: Missing 'message' field in Ollama response."));
            return;
        }

        QJsonObject messageObj = rootObj[QStringLiteral("message")].toObject();
        if (!messageObj.contains(QStringLiteral("content"))) {
            if (!m_history.isEmpty()) {
                m_history.removeAt(m_history.size() - 1);
            }
            emit errorOccurred(QStringLiteral("Invalid Response: Missing 'content' field in message object."));
            return;
        }

        QString responseContent = messageObj[QStringLiteral("content")].toString();

        QJsonObject assistantMessageObj;
        assistantMessageObj[QStringLiteral("role")] = QStringLiteral("assistant");
        assistantMessageObj[QStringLiteral("content")] = responseContent;
        m_history.append(assistantMessageObj);

        emit responseReceived(responseContent);
    });
}
