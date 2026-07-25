#include "llm/ollama_client.hpp"

#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

OllamaClient::OllamaClient(QObject *parent)
    : QObject(parent)
{
}

void OllamaClient::sendPrompt(const QString &prompt, const QString &model)
{
    QNetworkRequest request(m_endpointUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject userMessageObj;
    userMessageObj[QStringLiteral("role")] = QStringLiteral("user");
    userMessageObj[QStringLiteral("content")] = prompt;

    QJsonArray messagesArray;
    messagesArray.append(userMessageObj);

    QJsonObject payloadObj;
    payloadObj[QStringLiteral("model")] = model;
    payloadObj[QStringLiteral("messages")] = messagesArray;
    payloadObj[QStringLiteral("stream")] = false;

    QJsonDocument payloadDoc(payloadObj);
    QByteArray postData = payloadDoc.toJson(QJsonDocument::Compact);

    QNetworkReply *reply = m_networkManager.post(request, postData);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            QString errorMsg = QStringLiteral("Network error (%1): %2")
                                   .arg(reply->error())
                                   .arg(reply->errorString());
            emit errorOccurred(errorMsg);
            return;
        }

        QByteArray responseData = reply->readAll();
        QJsonParseError parseError;
        QJsonDocument responseDoc = QJsonDocument::fromJson(responseData, &parseError);

        if (parseError.error != QJsonParseError::NoError || !responseDoc.isObject()) {
            QString errorMsg = QStringLiteral("Invalid JSON response from Ollama server: %1")
                                   .arg(parseError.errorString());
            emit errorOccurred(errorMsg);
            return;
        }

        QJsonObject rootObj = responseDoc.object();

        if (rootObj.contains(QStringLiteral("error"))) {
            QString errorMsg = QStringLiteral("Ollama API Error: %1")
                                   .arg(rootObj[QStringLiteral("error")].toString());
            emit errorOccurred(errorMsg);
            return;
        }

        if (!rootObj.contains(QStringLiteral("message"))) {
            emit errorOccurred(QStringLiteral("Invalid Response: Missing 'message' field in Ollama response."));
            return;
        }

        QJsonObject messageObj = rootObj[QStringLiteral("message")].toObject();
        if (!messageObj.contains(QStringLiteral("content"))) {
            emit errorOccurred(QStringLiteral("Invalid Response: Missing 'content' field in message object."));
            return;
        }

        QString responseContent = messageObj[QStringLiteral("content")].toString();
        emit responseReceived(responseContent);
    });
}
