#ifndef TITANAI_OLLAMA_CLIENT_HPP
#define TITANAI_OLLAMA_CLIENT_HPP

#include <QObject>
#include <QString>
#include <QUrl>
#include <QJsonArray>
#include <QNetworkAccessManager>

class QNetworkReply;

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
    void processStreamData(QNetworkReply *reply);
    void handleStreamLine(const QByteArray &line);
    void finalizeResponse(QNetworkReply *reply);
    void rollbackLastUserMessage();
    void handleCompletionReply(QNetworkReply *reply);

    QNetworkAccessManager m_networkManager;
    QUrl m_endpointUrl{QStringLiteral("http://127.0.0.1:11434/api/chat")};
    QUrl m_warmupUrl{QStringLiteral("http://127.0.0.1:11434/api/generate")};
    QString m_model{QStringLiteral("gemma3:4b")};
    QJsonArray m_history;
    QByteArray m_streamBuffer;
    QString m_streamedContent;
    bool m_streamDone{false};
    bool m_streamFailed{false};
};

#endif // TITANAI_OLLAMA_CLIENT_HPP
