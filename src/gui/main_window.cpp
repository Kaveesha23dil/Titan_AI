#include "gui/main_window.hpp"
#include "gui/camera_dialog.hpp"
#include "gui/voice_settings_dialog.hpp"

#include <QBuffer>
#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTextBrowser>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_chatDisplay(new QTextBrowser(this))
    , m_input(new QLineEdit(this))
    , m_sendButton(new QPushButton(QStringLiteral("Send"), this))
    , m_statusLabel(new QLabel(QStringLiteral("Initializing local AI model..."), this))
{
    setWindowTitle(QStringLiteral("TitanAI"));
    resize(680, 520);

    m_chatDisplay->setReadOnly(true);
    m_chatDisplay->setOpenExternalLinks(false);

    auto *header = new QLabel(QStringLiteral("TitanAI - Local AI Assistant"), this);
    header->setAlignment(Qt::AlignCenter);
    QFont headerFont = header->font();
    headerFont.setBold(true);
    headerFont.setPointSize(headerFont.pointSize() + 4);
    header->setFont(headerFont);

    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #666; font-size: 11px;"));

    m_input->setPlaceholderText(QStringLiteral("Type a message... (Enter to send)"));
    m_input->setMinimumHeight(32);

    m_voiceButton = new QPushButton(QStringLiteral("Mic"), this);
    m_voiceButton->setCheckable(true);
    m_voiceButton->setToolTip(QStringLiteral("Start / stop voice input (push-to-talk style)"));
    m_voiceButton->setEnabled(false);

    m_voiceSettingsButton = new QPushButton(QStringLiteral("Voice Settings"), this);
    m_voiceSettingsButton->setToolTip(QStringLiteral("Configure voice input and spoken replies"));

    m_voiceStatusLabel = new QLabel(QStringLiteral("Voice disabled"), this);
    m_voiceStatusLabel->setStyleSheet(QStringLiteral("color:#6b7280; font-size:10px;"));

    m_micLevelBar = new QProgressBar(this);
    m_micLevelBar->setRange(0, 100);
    m_micLevelBar->setValue(0);
    m_micLevelBar->setTextVisible(false);
    m_micLevelBar->setFixedWidth(120);
    m_micLevelBar->setFixedHeight(10);
    m_micLevelBar->setStyleSheet(
        QStringLiteral("QProgressBar{border:1px solid #cbd5e1;border-radius:4px;"
                       "background:#f1f5f9;} QProgressBar::chunk{background:#4a90d9;"
                       "border-radius:4px;}"));
    m_micLevelBar->hide();

    auto *voiceStatusRow = new QHBoxLayout;
    voiceStatusRow->addWidget(m_voiceStatusLabel);
    voiceStatusRow->addStretch(1);
    voiceStatusRow->addWidget(m_micLevelBar);

    m_cameraButton = new QPushButton(QStringLiteral("Camera"), this);
    m_cameraButton->setToolTip(QStringLiteral("Open the camera to capture an image for analysis"));
    m_imageButton = new QPushButton(QStringLiteral("Image"), this);
    m_imageButton->setToolTip(QStringLiteral("Choose an image file to analyze"));

    auto *inputRow = new QHBoxLayout;
    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(m_voiceButton);
    inputRow->addWidget(m_voiceSettingsButton);
    inputRow->addWidget(m_cameraButton);
    inputRow->addWidget(m_imageButton);
    inputRow->addWidget(m_sendButton);

    m_pendingImageLabel = new QLabel(this);
    m_pendingImageLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_pendingImageLabel->setStyleSheet(QStringLiteral("color:#4a90d9; font-size:11px;"));
    m_pendingImageLabel->setTextFormat(Qt::RichText);
    m_pendingImageLabel->setVisible(false);

    m_clearImageButton = new QPushButton(QStringLiteral("Clear image"), this);
    m_clearImageButton->setVisible(false);

    auto *pendingImageRow = new QHBoxLayout;
    pendingImageRow->addWidget(m_pendingImageLabel);
    pendingImageRow->addStretch(1);
    pendingImageRow->addWidget(m_clearImageButton);

    auto *fixerBox = new QGroupBox(QStringLiteral("Developer Auto-Fix"), this);
    auto *fixerLayout = new QVBoxLayout(fixerBox);

    m_autoFixCheck = new QCheckBox(QStringLiteral("Auto-fix code errors"), fixerBox);
    auto *checkRow = new QHBoxLayout;
    checkRow->addWidget(m_autoFixCheck);
    checkRow->addStretch(1);

    m_projectEdit = new QLineEdit(fixerBox);
    m_projectEdit->setPlaceholderText(QStringLiteral("e.g. /home/you/my-project"));
    m_browseButton = new QPushButton(QStringLiteral("Browse"), fixerBox);
    auto *projectRow = new QHBoxLayout;
    projectRow->addWidget(new QLabel(QStringLiteral("Project:"), fixerBox));
    projectRow->addWidget(m_projectEdit, 1);
    projectRow->addWidget(m_browseButton);

    m_buildEdit = new QLineEdit(fixerBox);
    m_buildEdit->setPlaceholderText(QStringLiteral("e.g. cmake --build build   or   npm run build"));
    m_buildFixButton = new QPushButton(QStringLiteral("Build && Fix"), fixerBox);
    auto *buildRow = new QHBoxLayout;
    buildRow->addWidget(new QLabel(QStringLiteral("Build:"), fixerBox));
    buildRow->addWidget(m_buildEdit, 1);
    buildRow->addWidget(m_buildFixButton);

    fixerLayout->addLayout(checkRow);
    fixerLayout->addLayout(projectRow);
    fixerLayout->addLayout(buildRow);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->addWidget(header);
    layout->addWidget(m_statusLabel);
    layout->addWidget(fixerBox);
    layout->addWidget(m_chatDisplay, 1);
    layout->addLayout(voiceStatusRow);
    layout->addLayout(pendingImageRow);
    layout->addLayout(inputRow);
    setCentralWidget(central);

    connect(m_sendButton, &QPushButton::clicked, this, &MainWindow::onSendClicked);
    connect(m_input, &QLineEdit::returnPressed, this, &MainWindow::onSendClicked);
    connect(&m_agent, &Agent::responseChunkReceived, this, &MainWindow::onResponseChunk);
    connect(&m_agent, &Agent::responseReceived, this, &MainWindow::onResponseReceived);
    connect(&m_agent, &Agent::errorOccurred, this, &MainWindow::onErrorOccurred);
    connect(&m_agent, &Agent::installRequested, this, &MainWindow::onInstallRequested);
    connect(&m_agent, &Agent::toolOutputReceived, this, &MainWindow::onToolOutput);
    connect(&m_agent, &Agent::modelStatusChanged, this,
            [this](OllamaManager::Status, const QString &message) {
                m_statusLabel->setText(message);
            });
    connect(&m_agent, &Agent::modelReady, this, &MainWindow::onModelReady);
    connect(&m_agent, &Agent::modelError, this, &MainWindow::onModelError);

    m_projectDirectory = m_settings.value(QStringLiteral("projectDir")).toString();
    m_buildCommand = m_settings.value(QStringLiteral("buildCommand")).toString();
    const bool autoFixEnabled = m_settings.value(QStringLiteral("autoFixEnabled"), false).toBool();

    m_autoFixCheck->setChecked(autoFixEnabled);
    m_projectEdit->setText(m_projectDirectory);
    m_buildEdit->setText(m_buildCommand);
    m_agent.setAutoFixEnabled(autoFixEnabled);
    m_agent.setProjectDirectory(m_projectDirectory);
    m_agent.setBuildCommand(m_buildCommand);

    connect(m_autoFixCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_settings.setValue(QStringLiteral("autoFixEnabled"), checked);
        m_agent.setAutoFixEnabled(checked);
    });
    connect(m_projectEdit, &QLineEdit::editingFinished, this, [this]() {
        m_projectDirectory = m_projectEdit->text().trimmed();
        m_settings.setValue(QStringLiteral("projectDir"), m_projectDirectory);
        m_agent.setProjectDirectory(m_projectDirectory);
    });
    connect(m_buildEdit, &QLineEdit::editingFinished, this, [this]() {
        m_buildCommand = m_buildEdit->text().trimmed();
        m_settings.setValue(QStringLiteral("buildCommand"), m_buildCommand);
        m_agent.setBuildCommand(m_buildCommand);
    });
    connect(m_browseButton, &QPushButton::clicked, this, &MainWindow::onBrowseProject);
    connect(m_buildFixButton, &QPushButton::clicked, this, &MainWindow::onBuildAndFixClicked);
    connect(&m_agent, &Agent::autoFixEnabledChanged, this, [this](bool enabled) {
        m_settings.setValue(QStringLiteral("autoFixEnabled"), enabled);
        const QSignalBlocker blocker(m_autoFixCheck);
        m_autoFixCheck->setChecked(enabled);
    });
    connect(&m_agent, &Agent::codeFixStatus, this, [this](const QString &message) {
        appendMessage(QStringLiteral("TitanAI"), message, QStringLiteral("#4a90d9"));
    });
    connect(&m_agent, &Agent::codeFixFinished, this,
            [this](const QString &summary, bool success) {
                appendMessage(success ? QStringLiteral("TitanAI") : QStringLiteral("Error"),
                              summary,
                              success ? QStringLiteral("#4a90d9") : QStringLiteral("#c0392b"));
                setInputEnabled(true);
            });

    connect(m_voiceButton, &QPushButton::toggled, this, &MainWindow::onVoiceButtonToggled);
    connect(m_voiceSettingsButton, &QPushButton::clicked, this, &MainWindow::onVoiceSettings);

    connect(m_cameraButton, &QPushButton::clicked, this, &MainWindow::onCaptureFromCamera);
    connect(m_imageButton, &QPushButton::clicked, this, &MainWindow::onSelectImage);
    connect(m_clearImageButton, &QPushButton::clicked, this, &MainWindow::onClearPendingImage);
    connect(&m_agent, &Agent::cameraRequested, this, &MainWindow::onCaptureFromCamera);

    connect(&m_voiceEngine, &VoiceEngine::listeningChanged, this, [this](bool listening) {
        const QSignalBlocker blocker(m_voiceButton);
        m_voiceButton->setChecked(listening);
        m_voiceButton->setText(listening ? QStringLiteral("Stop") : QStringLiteral("Mic"));
        m_micLevelBar->setVisible(listening);
        if (!listening) {
            m_voiceStatusLabel->clear();
        }
    });
    connect(&m_voiceEngine, &VoiceEngine::partialTranscript, this, &MainWindow::onVoicePartial);
    connect(&m_voiceEngine, &VoiceEngine::finalTranscript, this, &MainWindow::onVoiceFinal);
    connect(&m_voiceEngine, &VoiceEngine::wakeWordDetected, this, [this]() {
        m_voiceStatusLabel->setText(QStringLiteral("Wake word detected - listening..."));
        m_micLevelBar->setVisible(true);
    });
    connect(&m_voiceEngine, &VoiceEngine::speakingChanged, this, [this](bool speaking) {
        m_voiceStatusLabel->setText(speaking ? QStringLiteral("Speaking...") : QString());
    });
    connect(&m_voiceEngine, &VoiceEngine::micLevelChanged, this,
            [this](float level) { m_micLevelBar->setValue(qRound(level * 100.0f)); });
    connect(&m_voiceEngine, &VoiceEngine::errorOccurred, this, &MainWindow::onVoiceError);
    connect(&m_voiceEngine, &VoiceEngine::sttStatusChanged, this,
            [this](const QString &message) { m_voiceStatusLabel->setText(message); });

    m_voiceEngine.setConfig(loadVoiceSettings());
    updateVoiceUi();

    appendMessage(QStringLiteral("TitanAI"),
                  QStringLiteral("Welcome! Loading the local AI model. You can start chatting once "
                                 "it is ready."),
                  QStringLiteral("#4a90d9"));

    setInputEnabled(false);
    m_agent.initializeModel(Agent::kDefaultModel);
}

void MainWindow::onSendClicked()
{
    if (!m_modelReady) {
        return;
    }

    QString text = m_input->text().trimmed();
    if (text.isEmpty() && m_pendingImage.isNull()) {
        return;
    }

    m_input->clear();
    appendMessage(QStringLiteral("You"), text, QStringLiteral("#2c3e50"));
    if (!m_pendingImage.isNull()) {
        appendImage(m_pendingImage);
    }
    setInputEnabled(false);

    if (m_pendingImage.isNull()) {
        m_agent.sendMessage(text);
    } else {
        const QImage image = m_pendingImage;
        m_pendingImage = QImage();
        updatePendingImageUi();
        if (text.isEmpty()) {
            text = QStringLiteral("Describe what is shown in this image.");
        }
        m_agent.sendImageMessage(image, text);
    }
}

void MainWindow::onModelReady(const QString &model)
{
    m_modelReady = true;
    m_statusLabel->setText(QStringLiteral("Model ready: %1").arg(model));
    appendMessage(QStringLiteral("TitanAI"),
                  QStringLiteral("Local AI model '%1' is ready. Ask me anything about your "
                                 "system, or just chat!")
                      .arg(model),
                  QStringLiteral("#4a90d9"));
    setInputEnabled(true);
}

void MainWindow::onModelError(const QString &error)
{
    m_modelReady = false;
    m_statusLabel->setText(QStringLiteral("Model unavailable"));
    appendMessage(QStringLiteral("Error"), error, QStringLiteral("#c0392b"));
    setInputEnabled(false);
}

void MainWindow::onResponseChunk(const QString &chunk)
{
    m_streamActive = true;

    if (!m_streamBlockStarted) {
        startStreamingBlock();
    }

    QTextCursor cursor = m_chatDisplay->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(chunk);
    m_chatDisplay->setTextCursor(cursor);
    m_chatDisplay->verticalScrollBar()->setValue(m_chatDisplay->verticalScrollBar()->maximum());
}

void MainWindow::onResponseReceived(const QString &response)
{
    if (!m_streamActive) {
        appendMessage(QStringLiteral("TitanAI"), response, QStringLiteral("#4a90d9"));
    }

    m_streamActive = false;
    m_streamBlockStarted = false;
    setInputEnabled(true);

    const VoiceEngine::Config voiceConfig = m_voiceEngine.config();
    if (voiceConfig.voiceEnabled && voiceConfig.readAloudEnabled) {
        m_voiceEngine.speak(response);
    }
}

void MainWindow::onErrorOccurred(const QString &error)
{
    m_streamActive = false;
    m_streamBlockStarted = false;
    appendMessage(QStringLiteral("Error"), error, QStringLiteral("#c0392b"));
    setInputEnabled(true);
}

void MainWindow::onInstallRequested(const QStringList &packages)
{
    QMessageBox::StandardButton answer =
        QMessageBox::question(this,
                              QStringLiteral("Confirm Installation"),
                              QStringLiteral("Install the following package(s) on your system?\n\n"
                                             "  %1\n\n"
                                             "This runs pacman with administrator privileges.")
                                  .arg(packages.join(QStringLiteral(", "))),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No);

    if (answer != QMessageBox::Yes) {
        setInputEnabled(true);
        return;
    }

    appendMessage(QStringLiteral("TitanAI"),
                  QStringLiteral("Installing %1...").arg(packages.join(QStringLiteral(", "))),
                  QStringLiteral("#4a90d9"));
    m_agent.performInstall(packages);
}

void MainWindow::onToolOutput(const QString &line)
{
    appendPlainLine(line, QStringLiteral("#6b7280"));
}

void MainWindow::onBrowseProject()
{
    const QString startDir =
        m_projectDirectory.isEmpty() ? QDir::homePath() : m_projectDirectory;
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select Project Directory"), startDir);
    if (directory.isEmpty()) {
        return;
    }
    m_projectDirectory = directory;
    m_projectEdit->setText(directory);
    m_settings.setValue(QStringLiteral("projectDir"), directory);
    m_agent.setProjectDirectory(directory);
}

void MainWindow::onBuildAndFixClicked()
{
    if (m_agent.isCodeFixBusy()) {
        return;
    }
    if (!m_modelReady) {
        appendMessage(QStringLiteral("Error"),
                      QStringLiteral("The model is not ready yet. Please wait."),
                      QStringLiteral("#c0392b"));
        return;
    }
    setInputEnabled(false);
    m_agent.runBuildAndFix();
}

void MainWindow::startStreamingBlock()
{
    m_chatDisplay->append(QStringLiteral("<b style=\"color:#4a90d9\">TitanAI:</b> "));
    m_streamBlockStarted = true;

    QTextCursor cursor = m_chatDisplay->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_chatDisplay->setTextCursor(cursor);
}

void MainWindow::setInputEnabled(bool enabled)
{
    m_input->setEnabled(enabled);
    m_sendButton->setEnabled(enabled);
    m_buildFixButton->setEnabled(enabled);
    if (enabled) {
        m_input->setFocus();
    }
}

void MainWindow::appendMessage(const QString &sender, const QString &text, const QString &color)
{
    QString escaped = text.toHtmlEscaped();
    escaped.replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
    m_chatDisplay->append(
        QStringLiteral("<b style=\"color:%1\">%2:</b> %3").arg(color, sender, escaped));
    m_chatDisplay->verticalScrollBar()->setValue(m_chatDisplay->verticalScrollBar()->maximum());
}

void MainWindow::appendPlainLine(const QString &text, const QString &color)
{
    m_chatDisplay->append(
        QStringLiteral("<span style=\"color:%1\">%2</span>").arg(color, text.toHtmlEscaped()));
    m_chatDisplay->verticalScrollBar()->setValue(m_chatDisplay->verticalScrollBar()->maximum());
}

void MainWindow::onVoicePartial(const QString &text)
{
    m_voiceStatusLabel->setText(QStringLiteral("... %1").arg(text));
}

void MainWindow::onVoiceFinal(const QString &text)
{
    m_input->setText(text);
    m_voiceStatusLabel->clear();

    if (m_voiceEngine.config().autoSendEnabled) {
        onSendClicked();
    }
}

void MainWindow::onVoiceError(const QString &error)
{
    appendMessage(QStringLiteral("Voice"), error, QStringLiteral("#c0392b"));
    m_voiceStatusLabel->setText(error);
}

void MainWindow::onVoiceSettings()
{
    VoiceSettingsDialog dialog(m_voiceEngine, this);
    if (dialog.exec() == QDialog::Accepted) {
        const VoiceEngine::Config config = dialog.config();
        m_voiceEngine.setConfig(config);
        if (config.voiceEnabled && config.wakeWordEnabled) {
            m_voiceEngine.startWakeWordListening();
        }
        saveVoiceSettings(config);
        updateVoiceUi();
    }
}

void MainWindow::onVoiceButtonToggled(bool enabled)
{
    if (enabled) {
        m_voiceEngine.startListening();
    } else {
        m_voiceEngine.stopListening();
    }
}

void MainWindow::onCaptureFromCamera()
{
    CameraDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        const QImage image = dialog.image();
        if (image.isNull()) {
            return;
        }
        m_pendingImage = image;
        updatePendingImageUi();
        appendMessage(QStringLiteral("TitanAI"),
                      QStringLiteral("Image captured. Ask your question about it and press Send."),
                      QStringLiteral("#4a90d9"));
    }
}

void MainWindow::onSelectImage()
{
    const QString file = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Select Image"),
        QDir::homePath(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp);;All Files (*)"));
    if (file.isEmpty()) {
        return;
    }

    QImage image(file);
    if (image.isNull()) {
        appendMessage(QStringLiteral("Error"),
                      QStringLiteral("Could not load the selected image."),
                      QStringLiteral("#c0392b"));
        return;
    }

    m_pendingImage = image;
    updatePendingImageUi();
    appendMessage(QStringLiteral("TitanAI"),
                  QStringLiteral("Image selected. Ask your question about it and press Send."),
                  QStringLiteral("#4a90d9"));
}

void MainWindow::onClearPendingImage()
{
    m_pendingImage = QImage();
    updatePendingImageUi();
}

void MainWindow::updatePendingImageUi()
{
    const bool hasImage = !m_pendingImage.isNull();
    m_pendingImageLabel->setVisible(hasImage);
    m_clearImageButton->setVisible(hasImage);
    if (hasImage) {
        QImage thumb = m_pendingImage.scaled(32, 32, Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        thumb.save(&buffer, "PNG");
        m_pendingImageLabel->setText(
            QStringLiteral("<img src=\"data:image/png;base64,%1\"/> Image attached "
                           "&mdash; type a question and press Send.")
                .arg(QString::fromLatin1(bytes.toBase64())));
    }
}

void MainWindow::appendImage(const QImage &image)
{
    QImage thumb = image.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    thumb.save(&buffer, "PNG");

    m_chatDisplay->append(
        QStringLiteral("<b style=\"color:#2c3e50\">You:</b><br/>"
                       "<img src=\"data:image/png;base64,%1\" style=\"max-width:100%;\"/>")
            .arg(QString::fromLatin1(bytes.toBase64())));
    m_chatDisplay->verticalScrollBar()->setValue(m_chatDisplay->verticalScrollBar()->maximum());
}

VoiceEngine::Config MainWindow::loadVoiceSettings()
{
    VoiceEngine::Config config;
    config.voiceEnabled = m_settings.value(QStringLiteral("voiceEnabled"), true).toBool();
    config.readAloudEnabled =
        m_settings.value(QStringLiteral("readAloudEnabled"), true).toBool();
    config.wakeWordEnabled =
        m_settings.value(QStringLiteral("wakeWordEnabled"), false).toBool();
    config.autoSendEnabled =
        m_settings.value(QStringLiteral("autoSendEnabled"), false).toBool();
    config.wakeWord =
        m_settings.value(QStringLiteral("wakeWord"), QStringLiteral("hey titan")).toString();
    config.sttModelPath = m_settings.value(QStringLiteral("sttModelPath")).toString();
    config.ttsVoice = m_settings.value(QStringLiteral("ttsVoice")).toString();
    config.ttsRate = m_settings.value(QStringLiteral("ttsRate"), 0.0).toDouble();
    config.ttsPitch = m_settings.value(QStringLiteral("ttsPitch"), 0.0).toDouble();
    config.ttsVolume = m_settings.value(QStringLiteral("ttsVolume"), 1.0).toDouble();
    return config;
}

void MainWindow::saveVoiceSettings(const VoiceEngine::Config &config)
{
    m_settings.setValue(QStringLiteral("voiceEnabled"), config.voiceEnabled);
    m_settings.setValue(QStringLiteral("readAloudEnabled"), config.readAloudEnabled);
    m_settings.setValue(QStringLiteral("wakeWordEnabled"), config.wakeWordEnabled);
    m_settings.setValue(QStringLiteral("autoSendEnabled"), config.autoSendEnabled);
    m_settings.setValue(QStringLiteral("wakeWord"), config.wakeWord);
    m_settings.setValue(QStringLiteral("sttModelPath"), config.sttModelPath);
    m_settings.setValue(QStringLiteral("ttsVoice"), config.ttsVoice);
    m_settings.setValue(QStringLiteral("ttsRate"), config.ttsRate);
    m_settings.setValue(QStringLiteral("ttsPitch"), config.ttsPitch);
    m_settings.setValue(QStringLiteral("ttsVolume"), config.ttsVolume);
}

void MainWindow::updateVoiceUi()
{
    const VoiceEngine::Config config = m_voiceEngine.config();
    const bool sttAvailable = VoiceEngine::sttAvailable();
    const bool ttsAvailable = m_voiceEngine.ttsAvailable();

    m_voiceButton->setEnabled(config.voiceEnabled && sttAvailable);

    if (!ttsAvailable) {
        m_voiceStatusLabel->setText(
            QStringLiteral("No text-to-speech engine found (install 'qt6-speech' or 'espeak-ng')"));
    } else if (!config.voiceEnabled) {
        m_voiceStatusLabel->setText(QStringLiteral("Voice disabled"));
    } else if (!sttAvailable) {
        m_voiceStatusLabel->setText(
            QStringLiteral("Voice input needs Vosk (install 'vosk-api')"));
    } else if (config.wakeWordEnabled) {
        m_voiceStatusLabel->setText(
            QStringLiteral("Wake word enabled: \"%1\"").arg(config.wakeWord));
    } else {
        m_voiceStatusLabel->setText(
            QStringLiteral("Voice ready (engine: %1)").arg(m_voiceEngine.ttsEngineName()));
    }
}
