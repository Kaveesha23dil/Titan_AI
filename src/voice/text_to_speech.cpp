#include "voice/text_to_speech.hpp"

#include <QLocale>
#include <QStandardPaths>
#include <QTextToSpeech>
#include <QVoice>

#include <cmath>

TextToSpeech::TextToSpeech(QObject *parent)
    : QObject(parent)
{
    const QStringList engines = QTextToSpeech::availableEngines();
    if (!engines.isEmpty()) {
        m_tts = new QTextToSpeech(engines.first(), this);
        if (!m_tts->availableVoices().isEmpty()) {
            m_engineName = engines.first();
            connect(m_tts, &QTextToSpeech::stateChanged, this,
                    [this](QTextToSpeech::State state) {
                        setSpeakingState(state == QTextToSpeech::Speaking);
                    });

            const QList<QVoice> voices = m_tts->availableVoices();
            for (const QVoice &voice : voices) {
                VoiceInfo info;
                info.id = voice.name();
                info.name = voice.name();
                info.language = QLocale::languageToString(voice.language());
                m_voices.append(info);
            }
            return;
        }
        delete m_tts;
        m_tts = nullptr;
    }

    useEspeakFallback();
}

TextToSpeech::~TextToSpeech()
{
    if (m_espeak) {
        m_espeak->kill();
        m_espeak->waitForFinished(1000);
    }
}

void TextToSpeech::useEspeakFallback()
{
    if (QStandardPaths::findExecutable(QStringLiteral("espeak-ng")).isEmpty()) {
        m_engineName = QStringLiteral("none");
        emit engineError(QStringLiteral("No text-to-speech engine found. Install 'qt6-speech' "
                                        "or 'espeak-ng' to enable spoken responses."));
        return;
    }

    m_engineName = QStringLiteral("espeak-ng");
    m_espeak = new QProcess(this);
    connect(m_espeak, &QProcess::finished, this, &TextToSpeech::onProcessFinished);

    VoiceInfo info;
    info.id = m_espeakVoice;
    info.name = QStringLiteral("espeak-ng default");
    info.language = QStringLiteral("en");
    m_voices.append(info);
}

bool TextToSpeech::isAvailable() const
{
    return m_engineName != QStringLiteral("none");
}

QString TextToSpeech::engineName() const
{
    return m_engineName;
}

QList<TextToSpeech::VoiceInfo> TextToSpeech::availableVoices() const
{
    return m_voices;
}

void TextToSpeech::setVoice(const QString &voiceName)
{
    if (voiceName.isEmpty()) {
        return;
    }

    if (m_tts) {
        const QList<QVoice> voices = m_tts->availableVoices();
        for (const QVoice &voice : voices) {
            if (voice.name() == voiceName) {
                m_tts->setVoice(voice);
                return;
            }
        }
    }

    m_espeakVoice = voiceName;
}

void TextToSpeech::setRate(double rate)
{
    m_rate = qBound(-1.0, rate, 1.0);
    if (m_tts) {
        m_tts->setRate(m_rate);
    }
}

void TextToSpeech::setPitch(double pitch)
{
    m_pitch = qBound(-1.0, pitch, 1.0);
    if (m_tts) {
        m_tts->setPitch(m_pitch);
    }
}

void TextToSpeech::setVolume(double volume)
{
    m_volume = qBound(0.0, volume, 1.0);
    if (m_tts) {
        m_tts->setVolume(m_volume);
    }
}

void TextToSpeech::speak(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || !isAvailable()) {
        return;
    }

    if (m_espeak) {
        speakEspeak(trimmed);
        return;
    }
    if (m_tts) {
        m_tts->say(trimmed);
    }
}

void TextToSpeech::speakEspeak(const QString &text)
{
    if (!m_espeak) {
        return;
    }

    m_espeak->kill();
    m_espeak->waitForFinished(500);

    const int rateWordsPerMinute = qRound(150.0 + m_rate * 80.0);
    const int pitch = qBound(0, qRound(50.0 + m_pitch * 50.0), 99);
    const int amplitude = qBound(0, qRound(100.0 + m_volume * 100.0), 200);

    m_espeak->start(QStringLiteral("espeak-ng"),
                    {QStringLiteral("-v"), m_espeakVoice,
                     QStringLiteral("-s"), QString::number(rateWordsPerMinute),
                     QStringLiteral("-p"), QString::number(pitch),
                     QStringLiteral("-a"), QString::number(amplitude),
                     text});
    setSpeakingState(true);
}

void TextToSpeech::stop()
{
    if (m_tts) {
        m_tts->stop();
    }
    if (m_espeak) {
        m_espeak->kill();
    }
}

bool TextToSpeech::isSpeaking() const
{
    return m_speaking;
}

void TextToSpeech::setSpeakingState(bool speaking)
{
    if (m_speaking == speaking) {
        return;
    }
    m_speaking = speaking;
    emit speakingChanged(speaking);
}

void TextToSpeech::onProcessFinished(int, QProcess::ExitStatus)
{
    setSpeakingState(false);
}
