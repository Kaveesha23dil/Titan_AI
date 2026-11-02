#include "rag/embedding_client.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {

constexpr int kRequestTimeoutMs = 120000;

QString extractOllamaError(const QByteArray &body)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
        const QJsonValue value = doc.object().value(QStringLiteral("error"));
        if (value.isString()) {
            return value.toString();
        }
        if (value.isObject()) {
            return value.toObject().value(QStringLiteral("message")).toString();
        }
    }
    return QString::fromUtf8(body).trimmed();
}

QVector<float> toVector(const QJsonArray &array)
{
    QVector<float> values;
    values.reserve(array.size());
    for (const QJsonValue &value : array) {
        values.append(static_cast<float>(value.toDouble()));
    }
    return values;
}

} // namespace

EmbeddingClient::EmbeddingClient(QObject *parent)
    : QObject(parent)
{
}

void EmbeddingClient::setModel(const QString &model)
{
    if (!model.trimmed().isEmpty()) {
        m_model = model.trimmed();
    }
}

const QString &EmbeddingClient::model() const
{
    return m_model;
}

void EmbeddingClient::requestEmbedding(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || m_busy) {
        return;
    }

    m_lastText = trimmed;
    m_busy = true;
    sendRequest(m_lastText, true);
}

void EmbeddingClient::sendRequest(const QString &text, bool useLegacyEndpoint)
{
    const QUrl url = useLegacyEndpoint
        ? QUrl(QStringLiteral("http://127.0.0.1:11434/api/embeddings"))
        : QUrl(QStringLiteral("http://127.0.0.1:11434/api/embed"));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(kRequestTimeoutMs);

    QJsonObject payload;
    payload[QStringLiteral("model")] = m_model;
    if (useLegacyEndpoint) {
        payload[QStringLiteral("prompt")] = text;
    } else {
        payload[QStringLiteral("input")] = text;
    }

    QNetworkReply *reply = m_networkManager.post(
        request, QJsonDocument(payload).toJson(QJsonDocument::Compact));

    connect(reply, &QNetworkReply::finished, this, [this, reply, useLegacyEndpoint]() {
        handleReply(reply, useLegacyEndpoint);
    });
}

void EmbeddingClient::handleReply(QNetworkReply *reply, bool usedLegacyEndpoint)
{
    reply->deleteLater();

    const int statusCode =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const QNetworkReply::NetworkError netError = reply->error();

    // The legacy endpoint may have been removed; retry once on the new API.
    if (usedLegacyEndpoint && (statusCode == 404 || netError == QNetworkReply::ContentNotFoundError)) {
        sendRequest(m_lastText, false);
        return;
    }

    m_busy = false;

    if (netError != QNetworkReply::NoError || statusCode >= 400) {
        QString message = extractOllamaError(body);
        if (message.isEmpty()) {
            message = reply->errorString();
        }
        emit embeddingError(message);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        emit embeddingError(QStringLiteral("Invalid embedding response: %1")
                                .arg(parseError.errorString()));
        return;
    }

    const QJsonObject root = doc.object();
    QJsonArray embeddingArray;
    if (root.contains(QStringLiteral("embedding"))) {
        embeddingArray = root.value(QStringLiteral("embedding")).toArray();
    } else if (root.contains(QStringLiteral("embeddings"))) {
        const QJsonArray embeddings = root.value(QStringLiteral("embeddings")).toArray();
        if (!embeddings.isEmpty()) {
            embeddingArray = embeddings.first().toArray();
        }
    }

    if (embeddingArray.isEmpty()) {
        emit embeddingError(QStringLiteral("The embedding model returned an empty vector."));
        return;
    }

    emit embeddingReady(toVector(embeddingArray));
}
