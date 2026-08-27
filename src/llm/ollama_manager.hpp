#ifndef TITANAI_OLLAMA_MANAGER_HPP
#define TITANAI_OLLAMA_MANAGER_HPP

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

class QProcess;

class OllamaManager : public QObject {
    Q_OBJECT

public:
    enum class Status {
        CheckingServer,
        StartingServer,
        Ready,
        Error
    };
    Q_ENUM(Status)

    explicit OllamaManager(QObject *parent = nullptr);
    ~OllamaManager() override;

    void ensureModelReady(const QString &model);
    [[nodiscard]] QString visionModel() const { return m_visionModel; }
    static bool isVisionCapable(const QString &modelName);

signals:
    void statusChanged(OllamaManager::Status status, const QString &message);
    void modelReady(const QString &model);
    void modelError(const QString &error);

private slots:
    void checkServerAndModel();

private:
    void startServer();
    void failWithError(const QString &error);

    QNetworkAccessManager m_networkManager;
    QProcess *m_serverProcess{nullptr};
    QTimer m_pollTimer;
    QUrl m_tagsUrl{QStringLiteral("http://127.0.0.1:11434/api/tags")};
    QString m_model;
    QString m_visionModel;
    int m_attempts{0};
    bool m_startedByUs{false};

    static constexpr int kMaxAttempts = 20;
    static constexpr int kPollIntervalMs = 750;
};

#endif // TITANAI_OLLAMA_MANAGER_HPP
