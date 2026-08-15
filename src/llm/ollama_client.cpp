#include "llm/ollama_client.hpp"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>

OllamaClient::OllamaClient(QObject *parent)
    : QObject(parent)
{
}

void OllamaClient::setModel(const QString &model)
{
    if (!model.trimmed().isEmpty()) {
        m_model = model.trimmed();
    }
}

const QString& OllamaClient::model() const
{
    return m_model;
}

void OllamaClient::clearHistory()
{
    m_history = QJsonArray();
}

const QJsonArray& OllamaClient::history() const
{
    return m_history;
}

void OllamaClient::warmUp()
{
    QNetworkRequest request(m_warmupUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject payloadObj;
    payloadObj[QStringLiteral("model")] = m_model;
    payloadObj[QStringLiteral("prompt")] = QString();
    payloadObj[QStringLiteral("stream")] = false;
    payloadObj[QStringLiteral("keep_alive")] = -1;

    QJsonDocument payloadDoc(payloadObj);
    QByteArray postData = payloadDoc.toJson(QJsonDocument::Compact);

    QNetworkReply *reply = m_networkManager.post(request, postData);
    connect(reply, &QNetworkReply::finished, this, [reply]() {
        reply->deleteLater();
    });
}

void OllamaClient::sendPrompt(const QString &prompt)
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
    payloadObj[QStringLiteral("model")] = m_model;
    payloadObj[QStringLiteral("messages")] = m_history;
    payloadObj[QStringLiteral("stream")] = true;
    payloadObj[QStringLiteral("keep_alive")] = -1;

    QJsonDocument payloadDoc(payloadObj);
    QByteArray postData = payloadDoc.toJson(QJsonDocument::Compact);

    m_streamBuffer.clear();
    m_streamedContent.clear();
    m_streamDone = false;
    m_streamFailed = false;

    QNetworkReply *reply = m_networkManager.post(request, postData);

    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        processStreamData(reply);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        processStreamData(reply);
        finalizeResponse(reply);
        reply->deleteLater();
    });
}

void OllamaClient::processStreamData(QNetworkReply *reply)
{
    m_streamBuffer.append(reply->readAll());

    while (true) {
        int newlineIdx = m_streamBuffer.indexOf('\n');
        if (newlineIdx == -1) {
            break;
        }

        QByteArray line = m_streamBuffer.left(newlineIdx).trimmed();
        m_streamBuffer.remove(0, newlineIdx + 1);

        if (!line.isEmpty()) {
            handleStreamLine(line);
        }
    }
}

void OllamaClient::handleStreamLine(const QByteArray &line)
{
    QJsonParseError parseError;
    QJsonDocument lineDoc = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !lineDoc.isObject()) {
        return;
    }

    QJsonObject obj = lineDoc.object();

    if (obj.contains(QStringLiteral("error"))) {
        m_streamFailed = true;
        emit errorOccurred(obj[QStringLiteral("error")].toString());
        return;
    }

    QString content = obj.value(QStringLiteral("message")).toObject()
                          .value(QStringLiteral("content")).toString();
    if (!content.isEmpty()) {
        m_streamedContent += content;
        emit responseChunkReceived(content);
    }

    if (obj.value(QStringLiteral("done")).toBool()) {
        m_streamDone = true;
    }
}

void OllamaClient::finalizeResponse(QNetworkReply *reply)
{
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError netError = reply->error();

    if (m_streamFailed) {
        rollbackLastUserMessage();
        return;
    }

    if (netError != QNetworkReply::NoError || statusCode >= 400) {
        QString errorDetails;
        if (statusCode > 0) {
            errorDetails += QStringLiteral("HTTP Status %1. ").arg(statusCode);
        }
        if (netError != QNetworkReply::NoError) {
            errorDetails += QStringLiteral("Network Error (%1): %2. ").arg(netError).arg(reply->errorString());
        }

        QByteArray responseData = reply->readAll();
        if (m_streamBuffer.isEmpty()) {
            QJsonParseError parseError;
            QJsonDocument errDoc = QJsonDocument::fromJson(responseData, &parseError);
            if (parseError.error == QJsonParseError::NoError && errDoc.isObject() && errDoc.object().contains(QStringLiteral("error"))) {
                errorDetails += QStringLiteral("Ollama Error: %1").arg(errDoc.object()[QStringLiteral("error")].toString());
            } else if (!responseData.isEmpty()) {
                errorDetails += QStringLiteral("Response Body: %1").arg(QString::fromUtf8(responseData));
            }
        }

        rollbackLastUserMessage();
        emit errorOccurred(errorDetails);
        return;
    }

    if (!m_streamDone) {
        rollbackLastUserMessage();
        emit errorOccurred(QStringLiteral("Stream ended unexpectedly before the response was complete."));
        return;
    }

    QJsonObject assistantMessageObj;
    assistantMessageObj[QStringLiteral("role")] = QStringLiteral("assistant");
    assistantMessageObj[QStringLiteral("content")] = m_streamedContent;
    m_history.append(assistantMessageObj);

    emit responseReceived(m_streamedContent);
}

void OllamaClient::rollbackLastUserMessage()
{
    if (!m_history.isEmpty()) {
        m_history.removeAt(m_history.size() - 1);
    }
}
