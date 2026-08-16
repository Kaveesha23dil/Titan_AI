#include "voice/speech_recognizer.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>

#ifdef TITANAI_HAVE_VOSK
#include <vosk_api.h>
#endif

namespace {

#ifdef TITANAI_HAVE_VOSK
QString extractJsonText(const QString &json)
{
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) {
        return {};
    }
    const QJsonObject obj = doc.object();
    const QString partial = obj.value(QStringLiteral("partial")).toString();
    const QString text = obj.value(QStringLiteral("text")).toString();
    return text.isEmpty() ? partial : text;
}
#endif

} // namespace

// ---------------------------------------------------------------------------
// VoskWorker
// ---------------------------------------------------------------------------

VoskWorker::VoskWorker(QObject *parent)
    : QObject(parent)
{
}

VoskWorker::~VoskWorker()
{
#ifdef TITANAI_HAVE_VOSK
    if (m_recognizer) {
        vosk_recognizer_free(m_recognizer);
        m_recognizer = nullptr;
    }
    if (m_model) {
        vosk_model_free(m_model);
        m_model = nullptr;
    }
#endif
}

void VoskWorker::startEngine(const QString &modelPath, int sampleRate)
{
#ifdef TITANAI_HAVE_VOSK
    if (sampleRate > 0 && sampleRate != m_sampleRate) {
        if (m_recognizer) {
            vosk_recognizer_free(m_recognizer);
            m_recognizer = nullptr;
        }
        m_sampleRate = sampleRate;
    }

    if (!m_model) {
        m_model = vosk_model_new(modelPath.toUtf8().constData());
        if (!m_model) {
            m_ready = false;
            emit errorOccurred(
                QStringLiteral("Failed to load the speech recognition model from: %1")
                    .arg(modelPath));
            return;
        }
    }

    if (!m_recognizer) {
        m_recognizer = vosk_recognizer_new(m_model, m_sampleRate);
        if (!m_recognizer) {
            m_ready = false;
            emit errorOccurred(QStringLiteral("Failed to create the speech recognizer."));
            return;
        }
    }

    m_ready = true;
#else
    Q_UNUSED(modelPath);
    Q_UNUSED(sampleRate);
    m_ready = false;
    emit errorOccurred(QStringLiteral("Speech recognition was built without Vosk support."));
#endif
}

void VoskWorker::setWakeWordEnabled(bool enabled, const QStringList &words)
{
    m_wakeEnabled = enabled;
    m_wakeWords = words;
    if (!enabled) {
        m_wakeSpotted = false;
    }
}

void VoskWorker::reset()
{
    m_wakeSpotted = false;
#ifdef TITANAI_HAVE_VOSK
    if (m_recognizer) {
        vosk_recognizer_reset(m_recognizer);
    }
#endif
}

void VoskWorker::processPcm(const QByteArray &pcm)
{
#ifdef TITANAI_HAVE_VOSK
    if (!m_ready || !m_recognizer || pcm.isEmpty()) {
        return;
    }

    if (vosk_recognizer_accept_waveform(m_recognizer, pcm.constData(),
                                        static_cast<int>(pcm.size()))) {
        const QString text = extractJsonText(QString::fromUtf8(vosk_recognizer_result(m_recognizer)));
        if (!text.isEmpty()) {
            emit finalText(text);
        }
    } else {
        const QString text =
            extractJsonText(QString::fromUtf8(vosk_recognizer_partial_result(m_recognizer)));
        if (m_wakeEnabled && !m_wakeSpotted && !text.isEmpty()) {
            for (const QString &word : m_wakeWords) {
                if (!word.isEmpty() && text.contains(word, Qt::CaseInsensitive)) {
                    m_wakeSpotted = true;
                    emit wakeWordDetected();
                    break;
                }
            }
        }
        if (!text.isEmpty()) {
            emit partial(text);
        }
    }
#else
    Q_UNUSED(pcm);
#endif
}

void VoskWorker::flush()
{
#ifdef TITANAI_HAVE_VOSK
    if (!m_ready || !m_recognizer) {
        return;
    }
    const QString text =
        extractJsonText(QString::fromUtf8(vosk_recognizer_final_result(m_recognizer)));
    if (!text.isEmpty()) {
        emit finalText(text);
    }
#endif
}

// ---------------------------------------------------------------------------
// SpeechRecognizer
// ---------------------------------------------------------------------------

SpeechRecognizer::SpeechRecognizer(QObject *parent)
    : QObject(parent)
{
    m_worker = new VoskWorker;
    m_worker->moveToThread(&m_thread);

    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &VoskWorker::partial, this, &SpeechRecognizer::partialTranscript);
    connect(m_worker, &VoskWorker::finalText, this, &SpeechRecognizer::finalTranscript);
    connect(m_worker, &VoskWorker::wakeWordDetected, this, &SpeechRecognizer::wakeWordDetected);
    connect(m_worker, &VoskWorker::errorOccurred, this, &SpeechRecognizer::errorOccurred);

    m_thread.start();
}

SpeechRecognizer::~SpeechRecognizer()
{
    m_thread.quit();
    m_thread.wait();
}

bool SpeechRecognizer::isAvailable()
{
#ifdef TITANAI_HAVE_VOSK
    return true;
#else
    return false;
#endif
}

void SpeechRecognizer::startEngine(const QString &modelPath, int sampleRate)
{
    QMetaObject::invokeMethod(m_worker, "startEngine", Qt::QueuedConnection,
                              Q_ARG(QString, modelPath), Q_ARG(int, sampleRate));
}

void SpeechRecognizer::setWakeWordEnabled(bool enabled, const QStringList &words)
{
    QMetaObject::invokeMethod(m_worker, "setWakeWordEnabled", Qt::QueuedConnection,
                              Q_ARG(bool, enabled), Q_ARG(QStringList, words));
}

void SpeechRecognizer::processAudio(const QByteArray &pcm)
{
    QMetaObject::invokeMethod(m_worker, "processPcm", Qt::QueuedConnection,
                              Q_ARG(QByteArray, pcm));
}

void SpeechRecognizer::reset()
{
    QMetaObject::invokeMethod(m_worker, "reset", Qt::QueuedConnection);
}

void SpeechRecognizer::flush()
{
    QMetaObject::invokeMethod(m_worker, "flush", Qt::QueuedConnection);
}
