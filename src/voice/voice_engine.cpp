#include "voice/voice_engine.hpp"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace {

constexpr int kMaxListenMs = 45000;
constexpr int kFlushGraceMs = 2000;

} // namespace

VoiceEngine::VoiceEngine(QObject *parent)
    : QObject(parent)
    , m_capture(this)
    , m_recognizer(this)
    , m_tts(this)
{
    m_listenTimer.setSingleShot(true);
    connect(&m_listenTimer, &QTimer::timeout, this, [this]() {
        if (m_listening) {
            m_recognizer.flush();
            finishListening();
        }
    });

    connect(&m_capture, &AudioCapture::audioChunk, this, &VoiceEngine::onAudioChunk);
    connect(&m_capture, &AudioCapture::speechEnded, this, &VoiceEngine::onSpeechEnded);
    connect(&m_capture, &AudioCapture::levelChanged, this, &VoiceEngine::micLevelChanged);
    connect(&m_capture, &AudioCapture::captureError, this, &VoiceEngine::errorOccurred);

    connect(&m_recognizer, &SpeechRecognizer::partialTranscript, this, &VoiceEngine::processPartial);
    connect(&m_recognizer, &SpeechRecognizer::finalTranscript, this, &VoiceEngine::processFinal);
    connect(&m_recognizer, &SpeechRecognizer::wakeWordDetected, this, &VoiceEngine::onWakeWordDetected);
    connect(&m_recognizer, &SpeechRecognizer::errorOccurred, this, &VoiceEngine::errorOccurred);

    connect(&m_tts, &TextToSpeech::speakingChanged, this, &VoiceEngine::onSpeakingChanged);
    connect(&m_tts, &TextToSpeech::engineError, this, &VoiceEngine::errorOccurred);
}

VoiceEngine::~VoiceEngine()
{
    finishListening();
}

bool VoiceEngine::sttAvailable()
{
    return SpeechRecognizer::isAvailable();
}

bool VoiceEngine::ttsAvailable() const
{
    return m_tts.isAvailable();
}

QString VoiceEngine::ttsEngineName() const
{
    return m_tts.engineName();
}

QList<TextToSpeech::VoiceInfo> VoiceEngine::ttsVoices() const
{
    return m_tts.availableVoices();
}

VoiceEngine::Config VoiceEngine::config() const
{
    return m_config;
}

void VoiceEngine::setConfig(const Config &config)
{
    if (m_listening || m_wakeActive) {
        stopListening();
    }

    m_config = config;

    m_tts.setRate(m_config.ttsRate);
    m_tts.setPitch(m_config.ttsPitch);
    m_tts.setVolume(m_config.ttsVolume);
    if (!m_config.ttsVoice.isEmpty()) {
        m_tts.setVoice(m_config.ttsVoice);
    }

    if (!m_config.voiceEnabled || !m_config.wakeWordEnabled) {
        if (m_wakeActive) {
            m_capture.stop();
            m_wakeActive = false;
            emit sttStatusChanged(QStringLiteral("Wake word disabled."));
        }
    }
}

bool VoiceEngine::ensureRecognizerStarted()
{
    if (!SpeechRecognizer::isAvailable()) {
        emit sttStatusChanged(QStringLiteral("Speech recognition (Vosk) is not installed. "
                                             "Install 'vosk-api' to enable voice input."));
        return false;
    }

    QString modelPath = m_config.sttModelPath;
    if (modelPath.isEmpty()) {
        modelPath = resolveDefaultModelPath();
    }
    if (modelPath.isEmpty()) {
        emit sttStatusChanged(QStringLiteral("No speech recognition model found. Set a Vosk "
                                             "model path in the voice settings."));
        return false;
    }

    const int rate = m_capture.sampleRate() > 0 ? m_capture.sampleRate() : 16000;
    m_recognizer.startEngine(modelPath, rate);
    return true;
}

QString VoiceEngine::resolveDefaultModelPath() const
{
    const QStringList candidates = {
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
            QStringLiteral("/vosk-model-small-en-us-0.15"),
        QStringLiteral("%1/.local/share/vosk-models/vosk-model-small-en-us-0.15")
            .arg(QDir::homePath()),
        QStringLiteral("/usr/share/vosk/vosk-model-small-en-us-0.15"),
        QStringLiteral("/usr/share/vosk/models/vosk-model-small-en-us-0.15"),
        QStringLiteral("/usr/share/vosk-models/vosk-model-small-en-us-0.15"),
    };

    for (const QString &candidate : candidates) {
        if (QDir(candidate).exists() || QFile::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

void VoiceEngine::startWakeWordListening()
{
    if (m_wakeActive || m_listening || isSpeaking()) {
        return;
    }
    if (!m_config.voiceEnabled || !m_config.wakeWordEnabled) {
        return;
    }
    if (!SpeechRecognizer::isAvailable()) {
        emit sttStatusChanged(QStringLiteral("Speech recognition (Vosk) is not installed; "
                                             "wake word is unavailable."));
        return;
    }

    m_capture.start();
    if (!ensureRecognizerStarted()) {
        m_capture.stop();
        return;
    }

    m_recognizer.setWakeWordEnabled(true, {m_config.wakeWord});
    m_recognizer.reset();
    m_wakeActive = true;
    emit sttStatusChanged(QStringLiteral("Listening for \"%1\"...").arg(m_config.wakeWord));
}

void VoiceEngine::startListening()
{
    if (!m_config.voiceEnabled || m_listening) {
        return;
    }

    if (m_wakeActive) {
        m_capture.stop();
        m_wakeActive = false;
    }
    if (isSpeaking()) {
        m_tts.stop();
    }

    if (!SpeechRecognizer::isAvailable()) {
        emit sttStatusChanged(QStringLiteral("Speech recognition (Vosk) is not installed."));
        return;
    }

    m_capture.start();
    if (!ensureRecognizerStarted()) {
        m_capture.stop();
        return;
    }

    m_recognizer.setWakeWordEnabled(false, {});
    m_recognizer.reset();
    m_listening = true;
    m_pendingStop = false;
    m_listenTimer.start(kMaxListenMs);

    emit listeningChanged(true);
    emit sttStatusChanged(QStringLiteral("Listening..."));
}

void VoiceEngine::stopListening()
{
    if (m_listening) {
        m_recognizer.flush();
    }
    finishListening();
}

bool VoiceEngine::isListening() const
{
    return m_listening;
}

void VoiceEngine::speak(const QString &text)
{
    if (!m_config.voiceEnabled || !m_config.readAloudEnabled) {
        return;
    }
    if (m_listening) {
        return;
    }
    m_tts.speak(text);
}

void VoiceEngine::stopSpeaking()
{
    m_tts.stop();
}

bool VoiceEngine::isSpeaking() const
{
    return m_tts.isSpeaking();
}

QString VoiceEngine::recognizedText() const
{
    return m_lastTranscript;
}

void VoiceEngine::onAudioChunk(const QByteArray &pcm)
{
    if (m_listening || m_wakeActive) {
        m_recognizer.processAudio(pcm);
    }
}

void VoiceEngine::onSpeechEnded()
{
    if (!m_listening) {
        return;
    }

    m_recognizer.flush();
    m_pendingStop = true;
    QTimer::singleShot(kFlushGraceMs, this, [this]() {
        if (m_pendingStop) {
            finishListening();
        }
    });
}

void VoiceEngine::onWakeWordDetected()
{
    if (!m_wakeActive) {
        return;
    }

    m_wakeActive = false;
    m_recognizer.setWakeWordEnabled(false, {});
    m_recognizer.reset();
    m_listening = true;
    m_pendingStop = false;
    m_listenTimer.start(kMaxListenMs);

    emit wakeWordDetected();
    emit listeningChanged(true);
    emit sttStatusChanged(QStringLiteral("Listening..."));
}

void VoiceEngine::onSpeakingChanged(bool speaking)
{
    emit speakingChanged(speaking);

    if (speaking) {
        if (m_wakeActive) {
            m_pauseWakeOnResume = true;
            m_capture.stop();
            m_wakeActive = false;
        }
    } else if (m_pauseWakeOnResume) {
        m_pauseWakeOnResume = false;
        startWakeWordListening();
    }
}

void VoiceEngine::processPartial(const QString &text)
{
    if (m_listening && !text.isEmpty()) {
        emit partialTranscript(text);
    }
}

void VoiceEngine::processFinal(const QString &text)
{
    m_pendingStop = false;
    finishListening();

    if (!text.isEmpty()) {
        m_lastTranscript = text;
        emit finalTranscript(text);
    }
}

void VoiceEngine::finishListening()
{
    m_pendingStop = false;
    m_capture.stop();
    m_listenTimer.stop();

    const bool wasListening = m_listening;
    m_listening = false;
    m_wakeActive = false;

    if (wasListening) {
        emit listeningChanged(false);
    }
}
