#ifndef TITANAI_AUDIO_CAPTURE_HPP
#define TITANAI_AUDIO_CAPTURE_HPP

#include <QAudioFormat>
#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>

class QAudioSource;
class QIODevice;

// Captures microphone audio through Qt Multimedia, converts every chunk to
// 16-bit mono PCM, and performs lightweight energy-based voice activity
// detection (VAD) on the live stream. Emits one audioChunk per read buffer so
// a downstream recognizer can consume the raw samples.
class AudioCapture : public QObject {
    Q_OBJECT

public:
    struct VadParams {
        double rmsThreshold = 350.0; // int16 RMS below this counts as silence
        int endOfSpeechMs = 750;     // silence before an utterance is considered finished
        int minSpeechMs = 250;       // ignore blips shorter than this
        int maxSpeechMs = 30000;     // hard stop for a single utterance
    };

    explicit AudioCapture(QObject *parent = nullptr);
    ~AudioCapture() override;

    void setVadParams(const VadParams &params);
    void start();
    void stop();
    bool isCapturing() const;
    int sampleRate() const;

signals:
    void audioChunk(const QByteArray &pcm16);
    void speechStarted();
    void speechEnded();
    void levelChanged(float level);
    void captureError(const QString &message);

private:
    void onReadyRead();
    double computeRms(const QByteArray &pcm) const;
    QByteArray toMonoInt16(const QByteArray &data) const;
    QAudioFormat pickFormat() const;

    QAudioSource *m_source{nullptr};
    QIODevice *m_io{nullptr};
    QAudioFormat m_format;
    VadParams m_vad;
    bool m_capturing{false};
    bool m_speechActive{false};
    QElapsedTimer m_lastVoiced;
    QElapsedTimer m_speechStart;
};

#endif // TITANAI_AUDIO_CAPTURE_HPP
