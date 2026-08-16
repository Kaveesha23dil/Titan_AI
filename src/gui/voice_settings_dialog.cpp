#include "gui/voice_settings_dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

VoiceSettingsDialog::VoiceSettingsDialog(VoiceEngine &engine, QWidget *parent)
    : QDialog(parent)
    , m_engine(engine)
    , m_config(engine.config())
{
    setWindowTitle(QStringLiteral("Voice Settings"));
    resize(480, 540);

    auto *root = new QVBoxLayout(this);

    auto *sttBox = new QGroupBox(QStringLiteral("Speech-to-Text"), this);
    auto *sttLayout = new QVBoxLayout(sttBox);

    m_enableVoiceCheck = new QCheckBox(QStringLiteral("Enable voice input (microphone)"), sttBox);
    m_enableVoiceCheck->setChecked(m_config.voiceEnabled);
    sttLayout->addWidget(m_enableVoiceCheck);

    m_autoSendCheck = new QCheckBox(QStringLiteral("Automatically send the message after speaking"),
                                    sttBox);
    m_autoSendCheck->setChecked(m_config.autoSendEnabled);
    sttLayout->addWidget(m_autoSendCheck);

    m_wakeWordCheck = new QCheckBox(QStringLiteral("Wake word (hands-free listening)"), sttBox);
    m_wakeWordCheck->setChecked(m_config.wakeWordEnabled);
    sttLayout->addWidget(m_wakeWordCheck);

    auto *wakeRow = new QHBoxLayout;
    wakeRow->addWidget(new QLabel(QStringLiteral("Wake word:"), sttBox));
    m_wakeWordEdit = new QLineEdit(m_config.wakeWord, sttBox);
    wakeRow->addWidget(m_wakeWordEdit, 1);
    sttLayout->addLayout(wakeRow);

    auto *modelRow = new QHBoxLayout;
    modelRow->addWidget(new QLabel(QStringLiteral("Model path:"), sttBox));
    m_modelEdit = new QLineEdit(m_config.sttModelPath, sttBox);
    m_modelEdit->setPlaceholderText(
        QStringLiteral("Vosk model directory (e.g. vosk-model-small-en-us-0.15)"));
    modelRow->addWidget(m_modelEdit, 1);
    m_browseButton = new QPushButton(QStringLiteral("Browse"), sttBox);
    modelRow->addWidget(m_browseButton);
    sttLayout->addLayout(modelRow);

    m_sttStatusLabel = new QLabel(sttBox);
    m_sttStatusLabel->setWordWrap(true);
    m_sttStatusLabel->setStyleSheet(QStringLiteral("color:#6b7280;"));
    if (!VoiceEngine::sttAvailable()) {
        m_sttStatusLabel->setText(
            QStringLiteral("Vosk is not installed. Install 'vosk-api' (AUR) and rebuild the "
                           "project to enable voice input."));
    } else if (m_config.sttModelPath.isEmpty()) {
        m_sttStatusLabel->setText(
            QStringLiteral("Download a model (e.g. vosk-model-small-en-us-0.15 from "
                           "alphacephei.com/vosk/models) and set its path here."));
    }
    sttLayout->addWidget(m_sttStatusLabel);

    root->addWidget(sttBox);

    auto *ttsBox = new QGroupBox(QStringLiteral("Text-to-Speech"), this);
    auto *ttsLayout = new QFormLayout(ttsBox);

    m_readAloudCheck = new QCheckBox(QStringLiteral("Read assistant responses aloud"), ttsBox);
    m_readAloudCheck->setChecked(m_config.readAloudEnabled);
    ttsLayout->addRow(m_readAloudCheck);

    m_voiceCombo = new QComboBox(ttsBox);
    const QList<TextToSpeech::VoiceInfo> voices = m_engine.ttsVoices();
    int selectedIndex = -1;
    for (int i = 0; i < voices.size(); ++i) {
        m_voiceCombo->addItem(QStringLiteral("%1 (%2)").arg(voices[i].name, voices[i].language),
                              voices[i].id);
        if (voices[i].id == m_config.ttsVoice) {
            selectedIndex = i;
        }
    }
    if (selectedIndex >= 0) {
        m_voiceCombo->setCurrentIndex(selectedIndex);
    }
    m_voiceCombo->setEnabled(!voices.isEmpty());
    ttsLayout->addRow(QStringLiteral("Voice:"), m_voiceCombo);

    m_rateSlider = new QSlider(Qt::Horizontal, ttsBox);
    m_rateSlider->setRange(-100, 100);
    m_rateSlider->setValue(qRound(m_config.ttsRate * 100.0));
    m_rateValue = new QLabel(QStringLiteral("%1%").arg(m_rateSlider->value()), ttsBox);
    auto *rateRow = new QHBoxLayout;
    rateRow->addWidget(m_rateSlider, 1);
    rateRow->addWidget(m_rateValue);
    ttsLayout->addRow(QStringLiteral("Rate:"), rateRow);

    m_pitchSlider = new QSlider(Qt::Horizontal, ttsBox);
    m_pitchSlider->setRange(-100, 100);
    m_pitchSlider->setValue(qRound(m_config.ttsPitch * 100.0));
    m_pitchValue = new QLabel(QStringLiteral("%1%").arg(m_pitchSlider->value()), ttsBox);
    auto *pitchRow = new QHBoxLayout;
    pitchRow->addWidget(m_pitchSlider, 1);
    pitchRow->addWidget(m_pitchValue);
    ttsLayout->addRow(QStringLiteral("Pitch:"), pitchRow);

    m_volumeSlider = new QSlider(Qt::Horizontal, ttsBox);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(qRound(m_config.ttsVolume * 100.0));
    m_volumeValue = new QLabel(QStringLiteral("%1%").arg(m_volumeSlider->value()), ttsBox);
    auto *volumeRow = new QHBoxLayout;
    volumeRow->addWidget(m_volumeSlider, 1);
    volumeRow->addWidget(m_volumeValue);
    ttsLayout->addRow(QStringLiteral("Volume:"), volumeRow);

    root->addWidget(ttsBox);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    connect(m_enableVoiceCheck, &QCheckBox::toggled, this, &VoiceSettingsDialog::onToggleVoice);
    connect(m_browseButton, &QPushButton::clicked, this, &VoiceSettingsDialog::onBrowseModel);
    connect(m_rateSlider, &QSlider::valueChanged, this,
            [this](int value) { m_rateValue->setText(QStringLiteral("%1%").arg(value)); });
    connect(m_pitchSlider, &QSlider::valueChanged, this,
            [this](int value) { m_pitchValue->setText(QStringLiteral("%1%").arg(value)); });
    connect(m_volumeSlider, &QSlider::valueChanged, this,
            [this](int value) { m_volumeValue->setText(QStringLiteral("%1%").arg(value)); });

    onToggleVoice(m_enableVoiceCheck->isChecked());
}

void VoiceSettingsDialog::onToggleVoice(bool enabled)
{
    m_autoSendCheck->setEnabled(enabled);
    m_wakeWordCheck->setEnabled(enabled);
    m_wakeWordEdit->setEnabled(enabled);
    m_modelEdit->setEnabled(enabled);
    m_browseButton->setEnabled(enabled);
}

void VoiceSettingsDialog::onBrowseModel()
{
    const QString startDir = m_modelEdit->text().isEmpty()
                                 ? QDir::homePath()
                                 : m_modelEdit->text();
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select Vosk Model Directory"), startDir);
    if (!directory.isEmpty()) {
        m_modelEdit->setText(directory);
    }
}

VoiceEngine::Config VoiceSettingsDialog::config() const
{
    VoiceEngine::Config config = m_config;
    config.voiceEnabled = m_enableVoiceCheck->isChecked();
    config.autoSendEnabled = m_autoSendCheck->isChecked();
    config.wakeWordEnabled = m_wakeWordCheck->isChecked();
    config.wakeWord = m_wakeWordEdit->text().trimmed().toLower();
    if (config.wakeWord.isEmpty()) {
        config.wakeWord = QStringLiteral("hey titan");
    }
    config.sttModelPath = m_modelEdit->text().trimmed();
    config.readAloudEnabled = m_readAloudCheck->isChecked();
    config.ttsVoice = m_voiceCombo->currentData().toString();
    config.ttsRate = m_rateSlider->value() / 100.0;
    config.ttsPitch = m_pitchSlider->value() / 100.0;
    config.ttsVolume = m_volumeSlider->value() / 100.0;
    return config;
}
