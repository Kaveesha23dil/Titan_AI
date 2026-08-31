#ifndef TITANAI_OLLAMA_CLIENT_HPP
#define TITANAI_OLLAMA_CLIENT_HPP

#include <QObject>
#include <QString>
#include <QUrl>
#include <QByteArray>
#include <QJsonArray>
#include <QNetworkAccessManager>

class QNetworkReply;

// Per-request streaming state. Scoped to each request (rather than shared member
// variables) so overlapping/streaming chat requests cannot corrupt one another.
struct StreamState {
    QByteArray buffer;
    QString streamedContent;
    bool done{false};
    bool failed{false};
};

class OllamaClient : public QObject {
    Q_OBJECT

public:
    explicit OllamaClient(QObject *parent = nullptr);
    ~OllamaClient() override = default;

    void sendPrompt(const QString &prompt);
    void sendImagePrompt(const QString &prompt, const QList<QByteArray> &encodedImages, const QString &modelOverride = QString());
    void requestCompletion(const QString &prompt);
    void requestImageCompletion(const QString &prompt, const QList<QByteArray> &encodedImages, const QString &modelOverride = QString());
    void setModel(const QString &model);
    [[nodiscard]] const QString& model() const;
    void warmUp();
    void unloadModel();
    void clearHistory();
    [[nodiscard]] const QJsonArray& history() const;

signals:
    void responseChunkReceived(const QString &chunk);
    void responseReceived(const QString &response);
    void errorOccurred(const QString &error);
    void completionReceived(const QString &response);
    void completionError(const QString &error);

private:
    void sendChatMessage(const QJsonObject &userMessageObj);
    void processStreamData(QNetworkReply *reply, StreamState &state);
    void handleStreamLine(const QByteArray &line, StreamState &state);
    void finalizeResponse(QNetworkReply *reply, StreamState &state);
    void rollbackLastUserMessage();
    void handleCompletionReply(QNetworkReply *reply);

    QNetworkAccessManager m_networkManager;
    QUrl m_endpointUrl{QStringLiteral("http://127.0.0.1:11434/api/chat")};
    QUrl m_warmupUrl{QStringLiteral("http://127.0.0.1:11434/api/generate")};
    QString m_model{QStringLiteral("gemma3:4b")};
    QJsonArray m_history;
};

#endif // TITANAI_OLLAMA_CLIENT_HPP
