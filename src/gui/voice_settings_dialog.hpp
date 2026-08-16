#ifndef TITANAI_VOICE_SETTINGS_DIALOG_HPP
#define TITANAI_VOICE_SETTINGS_DIALOG_HPP

#include <QDialog>

#include "voice/voice_engine.hpp"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;

// Modal dialog for configuring all voice-related options: speech-to-text
// (engine availability, model path, wake word, auto-send) and text-to-speech
// (voice, rate, pitch, volume).
class VoiceSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit VoiceSettingsDialog(VoiceEngine &engine, QWidget *parent = nullptr);

    VoiceEngine::Config config() const;

private slots:
    void onBrowseModel();
    void onToggleVoice(bool enabled);

private:
    VoiceEngine &m_engine;
    VoiceEngine::Config m_config;

    QCheckBox *m_enableVoiceCheck;
    QCheckBox *m_autoSendCheck;
    QCheckBox *m_wakeWordCheck;
    QLineEdit *m_wakeWordEdit;
    QLineEdit *m_modelEdit;
    QPushButton *m_browseButton;
    QLabel *m_sttStatusLabel;
    QCheckBox *m_readAloudCheck;
    QComboBox *m_voiceCombo;
    QSlider *m_rateSlider;
    QSlider *m_pitchSlider;
    QSlider *m_volumeSlider;
    QLabel *m_rateValue;
    QLabel *m_pitchValue;
    QLabel *m_volumeValue;
};

#endif // TITANAI_VOICE_SETTINGS_DIALOG_HPP
