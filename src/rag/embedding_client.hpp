#ifndef TITANAI_EMBEDDING_CLIENT_HPP
#define TITANAI_EMBEDDING_CLIENT_HPP

#include <QObject>
#include <QString>
#include <QVector>
#include <QNetworkAccessManager>

class QNetworkReply;

/**
 * @brief Minimal async client for Ollama's embedding endpoints.
 *
 * Uses the classic `/api/embeddings` endpoint (`prompt` -> `embedding`).
 * If the server reports that the endpoint is missing (HTTP 404), it retries
 * once against the newer `/api/embed` endpoint (`input` -> `embeddings[]`).
 *
 * Only one request is in flight at a time; the RAG manager uses two separate
 * instances (indexing and querying) so the two pipelines never interfere.
 */
class EmbeddingClient : public QObject {
    Q_OBJECT

public:
    explicit EmbeddingClient(QObject *parent = nullptr);
    ~EmbeddingClient() override = default;

    void setModel(const QString &model);
    [[nodiscard]] const QString &model() const;
    [[nodiscard]] bool isBusy() const { return m_busy; }

    void requestEmbedding(const QString &text);

signals:
    void embeddingReady(const QVector<float> &embedding);
    void embeddingError(const QString &error);

private:
    void sendRequest(const QString &text, bool useLegacyEndpoint);
    void handleReply(QNetworkReply *reply, bool usedLegacyEndpoint);

    QNetworkAccessManager m_networkManager;
    QString m_model{QStringLiteral("nomic-embed-text")};
    QString m_lastText;
    bool m_busy{false};
};

#endif // TITANAI_EMBEDDING_CLIENT_HPP
