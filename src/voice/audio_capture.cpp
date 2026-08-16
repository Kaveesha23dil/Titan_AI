#include "voice/audio_capture.hpp"

#include <QAudio>
#include <QAudioDevice>
#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>

#include <cmath>
#include <cstring>

AudioCapture::AudioCapture(QObject *parent)
    : QObject(parent)
{
}

AudioCapture::~AudioCapture()
{
    stop();
}

void AudioCapture::setVadParams(const VadParams &params)
{
    m_vad = params;
}

bool AudioCapture::isCapturing() const
{
    return m_capturing;
}

int AudioCapture::sampleRate() const
{
    return m_format.sampleRate();
}

QAudioFormat AudioCapture::pickFormat() const
{
    const QAudioDevice device = QMediaDevices::defaultAudioInput();

    const QList<int> rates = {16000, 48000, 44100, 22050, 8000};
    const QList<QAudioFormat::SampleFormat> formats = {
        QAudioFormat::Int16, QAudioFormat::Float, QAudioFormat::Int32};

    for (int rate : rates) {
        for (QAudioFormat::SampleFormat sampleFormat : formats) {
            QAudioFormat candidate;
            candidate.setSampleRate(rate);
            candidate.setChannelCount(1);
            candidate.setSampleFormat(sampleFormat);
            if (device.isFormatSupported(candidate)) {
                return candidate;
            }
        }
    }

    QAudioFormat preferred = device.preferredFormat();
    return device.isFormatSupported(preferred) ? preferred : QAudioFormat();
}

void AudioCapture::start()
{
    if (m_capturing) {
        return;
    }

    const QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (device.isNull()) {
        emit captureError(QStringLiteral("No microphone device was found."));
        return;
    }

    m_format = pickFormat();
    if (m_format.sampleRate() == 0 || m_format.channelCount() == 0) {
        emit captureError(QStringLiteral("The microphone format is not supported."));
        return;
    }

    m_source = new QAudioSource(device, m_format, this);
    m_source->setBufferSize(16384);

    m_io = m_source->start();
    if (!m_io) {
        emit captureError(QStringLiteral("Failed to start audio capture from the microphone."));
        m_source->deleteLater();
        m_source = nullptr;
        return;
    }

    connect(m_io, &QIODevice::readyRead, this, &AudioCapture::onReadyRead);
    connect(m_source, &QAudioSource::stateChanged, this, [this](QAudio::State state) {
        if (state == QAudio::StoppedState && m_capturing) {
            m_capturing = false;
            if (m_speechActive) {
                m_speechActive = false;
                emit speechEnded();
            }
        }
    });

    m_capturing = true;
    m_speechActive = false;
    m_lastVoiced.start();
    m_speechStart.start();
}

void AudioCapture::stop()
{
    if (!m_capturing && !m_source) {
        return;
    }

    if (m_source) {
        m_source->stop();
        m_source->deleteLater();
        m_source = nullptr;
    }
    m_io = nullptr;
    m_capturing = false;

    if (m_speechActive) {
        m_speechActive = false;
        emit speechEnded();
    }
}

QByteArray AudioCapture::toMonoInt16(const QByteArray &data) const
{
    const int channels = qMax(1, m_format.channelCount());
    const QAudioFormat::SampleFormat sampleFormat = m_format.sampleFormat();

    if (channels == 1 && sampleFormat == QAudioFormat::Int16) {
        return data;
    }

    int bytesPerSample = 2;
    switch (sampleFormat) {
    case QAudioFormat::UInt8:
        bytesPerSample = 1;
        break;
    case QAudioFormat::Int32:
    case QAudioFormat::Float:
        bytesPerSample = 4;
        break;
    default:
        bytesPerSample = 2;
        break;
    }

    const int frames = data.size() / (bytesPerSample * channels);
    QByteArray out;
    out.reserve(frames * 2);
    const char *p = data.constData();

    for (int f = 0; f < frames; ++f) {
        double sum = 0.0;
        for (int c = 0; c < channels; ++c) {
            const char *sp = p + (static_cast<qint64>(f) * channels + c) * bytesPerSample;
            double value = 0.0;
            switch (sampleFormat) {
            case QAudioFormat::UInt8: {
                quint8 sv = 0;
                std::memcpy(&sv, sp, 1);
                value = (static_cast<double>(sv) - 128.0) / 128.0;
                break;
            }
            case QAudioFormat::Int16: {
                qint16 sv = 0;
                std::memcpy(&sv, sp, 2);
                value = static_cast<double>(sv) / 32768.0;
                break;
            }
            case QAudioFormat::Int32: {
                qint32 sv = 0;
                std::memcpy(&sv, sp, 4);
                value = static_cast<double>(sv) / 2147483648.0;
                break;
            }
            case QAudioFormat::Float: {
                float sv = 0.0f;
                std::memcpy(&sv, sp, 4);
                value = static_cast<double>(sv);
                break;
            }
            default:
                value = 0.0;
                break;
            }
            sum += value;
        }

        double mono = sum / channels;
        mono = qBound(-1.0, mono, 1.0);
        const qint16 sample = static_cast<qint16>(mono * 32767.0);
        out.append(reinterpret_cast<const char *>(&sample), 2);
    }

    return out;
}

void AudioCapture::onReadyRead()
{
    if (!m_io) {
        return;
    }

    const QByteArray data = m_io->readAll();
    if (data.isEmpty()) {
        return;
    }

    const QByteArray pcm = toMonoInt16(data);
    emit audioChunk(pcm);

    const double rms = computeRms(pcm);
    emit levelChanged(static_cast<float>(qBound(0.0, rms / 2500.0, 1.0)));

    const bool voiced = rms >= m_vad.rmsThreshold;

    if (voiced) {
        m_lastVoiced.restart();
        if (!m_speechActive) {
            m_speechActive = true;
            m_speechStart.restart();
            emit speechStarted();
        }
    } else if (m_speechActive) {
        if (m_lastVoiced.elapsed() >= m_vad.endOfSpeechMs) {
            m_speechActive = false;
            emit speechEnded();
        }
    }

    if (m_speechActive && m_speechStart.elapsed() >= m_vad.maxSpeechMs) {
        m_speechActive = false;
        emit speechEnded();
    }
}

double AudioCapture::computeRms(const QByteArray &pcm) const
{
    if (pcm.size() < 2) {
        return 0.0;
    }

    const qint16 *samples = reinterpret_cast<const qint16 *>(pcm.constData());
    const int count = pcm.size() / 2;
    double sum = 0.0;
    for (int i = 0; i < count; ++i) {
        const double value = static_cast<double>(samples[i]) / 32768.0;
        sum += value * value;
    }
    return std::sqrt(sum / count) * 32768.0;
}
