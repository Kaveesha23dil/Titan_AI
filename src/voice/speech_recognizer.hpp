#ifndef TITANAI_SPEECH_RECOGNIZER_HPP
#define TITANAI_SPEECH_RECOGNIZER_HPP

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>

#ifdef TITANAI_HAVE_VOSK
typedef struct VoskModel VoskModel;
typedef struct VoskRecognizer VoskRecognizer;
#endif

// Worker object that lives on a dedicated thread. All Vosk calls happen here
// so the heavy DSP work never blocks the GUI. Without TITANAI_HAVE_VOSK this
// becomes a harmless no-op that reports an error on start.
class VoskWorker : public QObject {
    Q_OBJECT

public:
    explicit VoskWorker(QObject *parent = nullptr);
    ~VoskWorker() override;

public slots:
    void startEngine(const QString &modelPath, int sampleRate);
    void setWakeWordEnabled(bool enabled, const QStringList &words);
    void reset();
    void processPcm(const QByteArray &pcm);
    void flush();

signals:
    void partial(const QString &text);
    void finalText(const QString &text);
    void wakeWordDetected();
    void errorOccurred(const QString &message);

private:
    QString extractText(const QString &json);

#ifdef TITANAI_HAVE_VOSK
    VoskModel *m_model{nullptr};
    VoskRecognizer *m_recognizer{nullptr};
#endif
    int m_sampleRate{16000};
    bool m_wakeEnabled{false};
    bool m_wakeSpotted{false};
    QStringList m_wakeWords;
    bool m_ready{false};
};

// Public, thread-safe front end to the Vosk worker.
class SpeechRecognizer : public QObject {
    Q_OBJECT

public:
    explicit SpeechRecognizer(QObject *parent = nullptr);
    ~SpeechRecognizer() override;

    static bool isAvailable();

    void startEngine(const QString &modelPath, int sampleRate);
    void setWakeWordEnabled(bool enabled, const QStringList &words);
    void processAudio(const QByteArray &pcm);
    void reset();
    void flush();

signals:
    void partialTranscript(const QString &text);
    void finalTranscript(const QString &text);
    void wakeWordDetected();
    void errorOccurred(const QString &message);

private:
    QThread m_thread;
    VoskWorker *m_worker{nullptr};
};

#endif // TITANAI_SPEECH_RECOGNIZER_HPP
