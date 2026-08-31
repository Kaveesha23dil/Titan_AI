#ifndef TITANAI_OLLAMA_MANAGER_HPP
#define TITANAI_OLLAMA_MANAGER_HPP

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

class QNetworkReply;
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
    void refreshModels();
    [[nodiscard]] QString visionModel() const { return m_visionModel; }
    [[nodiscard]] const QStringList &installedModels() const { return m_installedModels; }
    static bool isVisionCapable(const QString &modelName);

signals:
    void statusChanged(OllamaManager::Status status, const QString &message);
    void modelReady(const QString &model);
    void modelError(const QString &error);
    void modelsChanged(const QStringList &models);

private slots:
    void checkServerAndModel();

private:
    void startServer();
    void failWithError(const QString &error);
    void handleTagsReply(QNetworkReply *reply);

    QString parseInstalled(const QString &fullName) const;

    QNetworkAccessManager m_networkManager;
    QProcess *m_serverProcess{nullptr};
    QTimer m_pollTimer;
    QUrl m_tagsUrl{QStringLiteral("http://127.0.0.1:11434/api/tags")};
    QString m_model;
    QString m_visionModel;
    QStringList m_installedModels;
    int m_attempts{0};
    bool m_startedByUs{false};

    static constexpr int kMaxAttempts = 20;
    static constexpr int kPollIntervalMs = 750;
};

#endif // TITANAI_OLLAMA_MANAGER_HPP
