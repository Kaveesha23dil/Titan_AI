#ifndef TITANAI_VOICE_ENGINE_HPP
#define TITANAI_VOICE_ENGINE_HPP

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "voice/audio_capture.hpp"
#include "voice/speech_recognizer.hpp"
#include "voice/text_to_speech.hpp"

// High-level orchestrator that ties microphone capture, energy-based VAD,
// wake-word spotting, speech-to-text, and text-to-speech together.
//
// Modes:
//   * Manual   - push-to-talk / toggle button; VAD ends the utterance and the
//                final transcript is delivered.
//   * Wake word - continuous keyword spotting; when the wake word is heard the
//                engine switches to full recognition and delivers a transcript.
//
// Text-to-speech is paused whenever the engine is listening so the assistant
// never talks over the user (and wake word never fires on its own voice).
class VoiceEngine : public QObject {
    Q_OBJECT

public:
    struct Config {
        bool voiceEnabled = true;
        bool readAloudEnabled = true;
        bool wakeWordEnabled = false;
        bool autoSendEnabled = false;
        QString wakeWord = QStringLiteral("hey titan");
        QString sttModelPath;
        QString ttsVoice;
        double ttsRate = 0.0;
        double ttsPitch = 0.0;
        double ttsVolume = 1.0;
    };

    explicit VoiceEngine(QObject *parent = nullptr);
    ~VoiceEngine() override;

    static bool sttAvailable();
    bool ttsAvailable() const;
    QString ttsEngineName() const;
    QList<TextToSpeech::VoiceInfo> ttsVoices() const;

    void setConfig(const Config &config);
    Config config() const;

    void startListening();
    void stopListening();
    bool isListening() const;

    // Starts wake-word spotting. This is only ever called in response to an
    // explicit user action (e.g. enabling the wake word in Voice Settings) so
    // the voice assistant stays independent of the AI assistant startup.
    void startWakeWordListening();

    void speak(const QString &text);
    void stopSpeaking();
    bool isSpeaking() const;

    QString recognizedText() const;

signals:
    void listeningChanged(bool listening);
    void partialTranscript(const QString &text);
    void finalTranscript(const QString &text);
    void wakeWordDetected();
    void speakingChanged(bool speaking);
    void micLevelChanged(float level);
    void errorOccurred(const QString &message);
    void sttStatusChanged(const QString &message);

private:
    bool ensureRecognizerStarted();
    void onAudioChunk(const QByteArray &pcm);
    void onSpeechEnded();
    void onWakeWordDetected();
    void onSpeakingChanged(bool speaking);
    void processPartial(const QString &text);
    void processFinal(const QString &text);
    void finishListening();
    QString resolveDefaultModelPath() const;

    Config m_config;
    AudioCapture m_capture;
    SpeechRecognizer m_recognizer;
    TextToSpeech m_tts;
    QTimer m_listenTimer;
    QString m_lastTranscript;
    bool m_listening{false};
    bool m_wakeActive{false};
    bool m_pendingStop{false};
    bool m_pauseWakeOnResume{false};
};

#endif // TITANAI_VOICE_ENGINE_HPP
