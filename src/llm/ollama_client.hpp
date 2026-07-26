#ifndef TITANAI_OLLAMA_CLIENT_HPP
#define TITANAI_OLLAMA_CLIENT_HPP

#include <QObject>
#include <QString>
#include <QUrl>
#include <QJsonArray>
#include <QNetworkAccessManager>

class OllamaClient : public QObject {
    Q_OBJECT

public:
    explicit OllamaClient(QObject *parent = nullptr);
    ~OllamaClient() override = default;

    void sendPrompt(const QString &prompt, const QString &model = QStringLiteral("gemma3:4b"));
    void clearHistory();
    [[nodiscard]] const QJsonArray& history() const;

signals:
    void responseReceived(const QString &response);
    void errorOccurred(const QString &error);

private:
    QNetworkAccessManager m_networkManager;
    QUrl m_endpointUrl{QStringLiteral("http://127.0.0.1:11434/api/chat")};
    QJsonArray m_history;
};

#endif // TITANAI_OLLAMA_CLIENT_HPP
