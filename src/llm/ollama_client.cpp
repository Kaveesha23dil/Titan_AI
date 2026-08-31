#include "llm/ollama_client.hpp"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>

#include <memory>

namespace {

QJsonObject defaultOllamaOptions()
{
    QJsonObject opts;
    opts[QStringLiteral("num_ctx")] = 1536;    // Optimal context buffer (saves ~25% RAM)
    opts[QStringLiteral("num_thread")] = 4;   // 4 physical CPU cores (prevents hyperthreading thread contention and CPU lockup)
    opts[QStringLiteral("num_batch")] = 256;  // Smaller batch size saves compute buffer memory
    return opts;
}

const QString kDefaultKeepAlive = QStringLiteral("5m"); // Automatically unload model from RAM after 5 min idle

} // namespace

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
    payloadObj[QStringLiteral("keep_alive")] = kDefaultKeepAlive;
    payloadObj[QStringLiteral("options")] = defaultOllamaOptions();

    QJsonDocument payloadDoc(payloadObj);
    QByteArray postData = payloadDoc.toJson(QJsonDocument::Compact);

    QNetworkReply *reply = m_networkManager.post(request, postData);
    connect(reply, &QNetworkReply::finished, this, [reply]() {
        reply->deleteLater();
    });
}

void OllamaClient::unloadModel()
{
    QNetworkRequest request(m_warmupUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject payloadObj;
    payloadObj[QStringLiteral("model")] = m_model;
    payloadObj[QStringLiteral("keep_alive")] = 0; // 0 immediately evicts model weights from RAM

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

    sendChatMessage(userMessageObj);
}

void OllamaClient::sendImagePrompt(const QString &prompt, const QList<QByteArray> &encodedImages, const QString &modelOverride)
{
    QString trimmedPrompt = prompt.trimmed();
    if (trimmedPrompt.isEmpty()) {
        return;
    }

    QJsonObject userMessageObj;
    userMessageObj[QStringLiteral("role")] = QStringLiteral("user");
    userMessageObj[QStringLiteral("content")] = trimmedPrompt;

    if (!encodedImages.isEmpty()) {
        QJsonArray imagesArray;
        for (const QByteArray &image : encodedImages) {
            imagesArray.append(QString::fromLatin1(image));
        }
        userMessageObj[QStringLiteral("images")] = imagesArray;
    }

    QString previousModel = m_model;
    if (!modelOverride.trimmed().isEmpty()) {
        m_model = modelOverride.trimmed();
    }

    sendChatMessage(userMessageObj);

    if (!modelOverride.trimmed().isEmpty()) {
        m_model = previousModel;
    }
}

void OllamaClient::sendChatMessage(const QJsonObject &userMessageObj)
{
    m_history.append(userMessageObj);

    // Keep history bounded to last 10 messages to prevent exponential memory/CPU growth
    constexpr int kMaxHistoryEntries = 10;
    if (m_history.size() > kMaxHistoryEntries) {
        QJsonArray trimmedHistory;
        const int startIndex = m_history.size() - kMaxHistoryEntries;
        for (int i = startIndex; i < m_history.size(); ++i) {
            trimmedHistory.append(m_history.at(i));
        }
        m_history = trimmedHistory;
    }

    QNetworkRequest request(m_endpointUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonArray messagesWithSystem;
    QJsonObject systemMsg;
    systemMsg[QStringLiteral("role")] = QStringLiteral("system");
    systemMsg[QStringLiteral("content")] = QStringLiteral(
        "You are TitanAI, a fast, knowledgeable local AI coding and system assistant for Arch Linux. "
        "Provide expert solutions for programming, debugging, software architecture, and system administration. "
        "Use clean markdown formatting, concise explanations, and high-quality code.");
    messagesWithSystem.append(systemMsg);
    for (const QJsonValue &val : m_history) {
        messagesWithSystem.append(val);
    }

    QJsonObject payloadObj;
    payloadObj[QStringLiteral("model")] = m_model;
    payloadObj[QStringLiteral("messages")] = messagesWithSystem;
    payloadObj[QStringLiteral("stream")] = true;
    payloadObj[QStringLiteral("keep_alive")] = kDefaultKeepAlive;
    payloadObj[QStringLiteral("options")] = defaultOllamaOptions();

    QJsonDocument payloadDoc(payloadObj);
    QByteArray postData = payloadDoc.toJson(QJsonDocument::Compact);

    // Stream state is scoped to this request so overlapping chat requests
    // cannot corrupt one another's buffers.
    auto state = std::make_shared<StreamState>();

    QNetworkReply *reply = m_networkManager.post(request, postData);

    connect(reply, &QNetworkReply::readyRead, this, [this, reply, state]() {
        processStreamData(reply, *state);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, state]() {
        processStreamData(reply, *state);
        finalizeResponse(reply, *state);
        reply->deleteLater();
    });
}

void OllamaClient::requestCompletion(const QString &prompt)
{
    QString trimmedPrompt = prompt.trimmed();
    if (trimmedPrompt.isEmpty()) {
        return;
    }

    QNetworkRequest request(m_endpointUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonArray messages;
    QJsonObject userMessageObj;
    userMessageObj[QStringLiteral("role")] = QStringLiteral("user");
    userMessageObj[QStringLiteral("content")] = trimmedPrompt;
    messages.append(userMessageObj);

    QJsonObject payloadObj;
    payloadObj[QStringLiteral("model")] = m_model;
    payloadObj[QStringLiteral("messages")] = messages;
    payloadObj[QStringLiteral("stream")] = false;
    payloadObj[QStringLiteral("keep_alive")] = kDefaultKeepAlive;
    payloadObj[QStringLiteral("options")] = defaultOllamaOptions();

    QNetworkReply *reply = m_networkManager.post(
        request, QJsonDocument(payloadObj).toJson(QJsonDocument::Compact));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleCompletionReply(reply);
        reply->deleteLater();
    });
}

void OllamaClient::requestImageCompletion(const QString &prompt,
                                          const QList<QByteArray> &encodedImages,
                                          const QString &modelOverride)
{
    QString trimmedPrompt = prompt.trimmed();
    if (trimmedPrompt.isEmpty()) {
        return;
    }

    const QString targetModel = modelOverride.trimmed().isEmpty() ? m_model : modelOverride.trimmed();

    QNetworkRequest request(m_endpointUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QJsonObject userMessageObj;
    userMessageObj[QStringLiteral("role")]    = QStringLiteral("user");
    userMessageObj[QStringLiteral("content")] = trimmedPrompt;

    if (!encodedImages.isEmpty()) {
        QJsonArray imagesArray;
        for (const QByteArray &img : encodedImages) {
            imagesArray.append(QString::fromLatin1(img));
        }
        userMessageObj[QStringLiteral("images")]  = imagesArray;
    }

    QJsonArray messages;
    messages.append(userMessageObj);

    QJsonObject payloadObj;
    payloadObj[QStringLiteral("model")]      = targetModel;
    payloadObj[QStringLiteral("messages")]   = messages;
    payloadObj[QStringLiteral("stream")]     = false;
    payloadObj[QStringLiteral("keep_alive")] = kDefaultKeepAlive;
    payloadObj[QStringLiteral("options")]    = defaultOllamaOptions();

    QNetworkReply *reply = m_networkManager.post(
        request, QJsonDocument(payloadObj).toJson(QJsonDocument::Compact));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleCompletionReply(reply);
        reply->deleteLater();
    });
}

void OllamaClient::handleCompletionReply(QNetworkReply *reply)
{
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray responseData = reply->readAll();

    if (reply->error() != QNetworkReply::NoError || statusCode >= 400) {
        QString errorDetails;
        if (statusCode > 0) {
            errorDetails += QStringLiteral("HTTP Status %1. ").arg(statusCode);
        }

        // Try extracting structured error from Ollama JSON response
        QJsonParseError parseError;
        QJsonDocument errDoc = QJsonDocument::fromJson(responseData, &parseError);
        if (parseError.error == QJsonParseError::NoError && errDoc.isObject()) {
            QJsonObject rootObj = errDoc.object();
            if (rootObj.contains(QStringLiteral("error"))) {
                QJsonValue errVal = rootObj.value(QStringLiteral("error"));
                if (errVal.isObject()) {
                    errorDetails += errVal.toObject().value(QStringLiteral("message")).toString();
                } else if (errVal.isString()) {
                    QString errStr = errVal.toString();
                    QJsonDocument nestedDoc = QJsonDocument::fromJson(errStr.toUtf8());
                    if (nestedDoc.isObject() && nestedDoc.object().contains(QStringLiteral("error"))) {
                        errorDetails += nestedDoc.object().value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
                    } else {
                        errorDetails += errStr;
                    }
                }
            }
        }

        if (errorDetails.isEmpty() || errorDetails == QStringLiteral("HTTP Status %1. ").arg(statusCode)) {
            if (reply->error() != QNetworkReply::NoError) {
                errorDetails += QStringLiteral("Network Error (%1): %2")
                                    .arg(reply->error())
                                    .arg(reply->errorString());
            }
        }

        emit completionError(errorDetails);
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        emit completionError(QStringLiteral("Invalid response from Ollama: %1").arg(parseError.errorString()));
        return;
    }

    QJsonObject rootObj = doc.object();
    QString content;
    if (rootObj.contains(QStringLiteral("message"))) {
        content = rootObj.value(QStringLiteral("message")).toObject()
                         .value(QStringLiteral("content")).toString();
    } else if (rootObj.contains(QStringLiteral("response"))) {
        content = rootObj.value(QStringLiteral("response")).toString();
    }

    if (content.trimmed().isEmpty()) {
        emit completionError(QStringLiteral("The model returned an empty response."));
        return;
    }

    emit completionReceived(content);
}

void OllamaClient::processStreamData(QNetworkReply *reply, StreamState &state)
{
    state.buffer.append(reply->readAll());

    while (true) {
        int newlineIdx = state.buffer.indexOf('\n');
        if (newlineIdx == -1) {
            break;
        }

        QByteArray line = state.buffer.left(newlineIdx).trimmed();
        state.buffer.remove(0, newlineIdx + 1);

        if (!line.isEmpty()) {
            handleStreamLine(line, state);
        }
    }
}

void OllamaClient::handleStreamLine(const QByteArray &line, StreamState &state)
{
    QJsonParseError parseError;
    QJsonDocument lineDoc = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !lineDoc.isObject()) {
        return;
    }

    QJsonObject obj = lineDoc.object();

    if (obj.contains(QStringLiteral("error"))) {
        state.failed = true;
        emit errorOccurred(obj[QStringLiteral("error")].toString());
        return;
    }

    QString content = obj.value(QStringLiteral("message")).toObject()
                          .value(QStringLiteral("content")).toString();
    if (!content.isEmpty()) {
        state.streamedContent += content;
        emit responseChunkReceived(content);
    }

    if (obj.value(QStringLiteral("done")).toBool()) {
        state.done = true;
    }
}

void OllamaClient::finalizeResponse(QNetworkReply *reply, StreamState &state)
{
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QNetworkReply::NetworkError netError = reply->error();

    if (state.failed) {
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
        if (state.buffer.isEmpty()) {
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

    if (!state.done) {
        rollbackLastUserMessage();
        emit errorOccurred(QStringLiteral("Stream ended unexpectedly before the response was complete."));
        return;
    }

    QJsonObject assistantMessageObj;
    assistantMessageObj[QStringLiteral("role")] = QStringLiteral("assistant");
    assistantMessageObj[QStringLiteral("content")] = state.streamedContent;
    m_history.append(assistantMessageObj);

    emit responseReceived(state.streamedContent);
}

void OllamaClient::rollbackLastUserMessage()
{
    if (!m_history.isEmpty()) {
        m_history.removeAt(m_history.size() - 1);
    }
}
