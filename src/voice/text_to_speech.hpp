#ifndef TITANAI_TEXT_TO_SPEECH_HPP
#define TITANAI_TEXT_TO_SPEECH_HPP

#include <QObject>
#include <QProcess>
#include <QString>

class QTextToSpeech;
class QVoice;

// Thin wrapper around Qt's QTextToSpeech (speech-dispatcher backend) with an
// automatic fallback to the lightweight `espeak-ng` command-line synthesizer.
// Exposes speaking state so callers can pause listening while audio plays.
class TextToSpeech : public QObject {
    Q_OBJECT

public:
    struct VoiceInfo {
        QString id;
        QString name;
        QString language;
    };

    explicit TextToSpeech(QObject *parent = nullptr);
    ~TextToSpeech() override;

    bool isAvailable() const;
    QString engineName() const;
    QList<VoiceInfo> availableVoices() const;

    void setVoice(const QString &voiceName);
    void setRate(double rate);    // -1.0 .. 1.0
    void setPitch(double pitch);  // -1.0 .. 1.0
    void setVolume(double volume); //  0.0 .. 1.0

    void speak(const QString &text);
    void stop();
    bool isSpeaking() const;

signals:
    void speakingChanged(bool speaking);
    void engineError(const QString &message);

private:
    void useEspeakFallback();
    bool tryInitEngine(const QString &engine);
    bool engineRuntimeAvailable(const QString &engine) const;
    void setSpeakingState(bool speaking);
    void speakEspeak(const QString &text);
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

    QTextToSpeech *m_tts{nullptr};
    QProcess *m_espeak{nullptr};
    QString m_engineName;
    QString m_espeakVoice{QStringLiteral("en-us")};
    QList<VoiceInfo> m_voices;
    double m_rate{0.0};
    double m_pitch{0.0};
    double m_volume{1.0};
    bool m_speaking{false};
};

#endif // TITANAI_TEXT_TO_SPEECH_HPP
