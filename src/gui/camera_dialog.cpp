#include "gui/camera_dialog.hpp"

#include <QCamera>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImageCapture>
#include <QLabel>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVideoWidget>

CameraDialog::CameraDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Camera"));
    resize(640, 540);

    auto *layout = new QVBoxLayout(this);

    m_videoWidget = new QVideoWidget(this);
    m_videoWidget->setMinimumHeight(300);
    layout->addWidget(m_videoWidget, 1);

    m_previewLabel = new QLabel(this);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setFixedHeight(130);
    m_previewLabel->setStyleSheet(QStringLiteral("border:1px solid #cbd5e1; color:#6b7280;"));
    m_previewLabel->setText(QStringLiteral("No image captured yet"));
    layout->addWidget(m_previewLabel);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QStringLiteral("color:#6b7280;"));
    layout->addWidget(m_statusLabel);

    m_captureButton = new QPushButton(QStringLiteral("Capture"), this);
    m_captureButton->setEnabled(false);
    auto *chooseButton = new QPushButton(QStringLiteral("Choose File..."), this);
    m_okButton = new QPushButton(QStringLiteral("Use Image"), this);
    m_okButton->setEnabled(false);
    auto *cancelButton = new QPushButton(QStringLiteral("Cancel"), this);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->addWidget(m_captureButton);
    buttonRow->addWidget(chooseButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_okButton);
    buttonRow->addWidget(cancelButton);
    layout->addLayout(buttonRow);

    connect(m_captureButton, &QPushButton::clicked, this, &CameraDialog::onCapture);
    connect(chooseButton, &QPushButton::clicked, this, &CameraDialog::onChooseFile);
    connect(m_okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    if (QMediaDevices::videoInputs().isEmpty()) {
        m_videoWidget->hide();
        m_statusLabel->setText(QStringLiteral("No camera detected. You can still choose an image "
                                              "file instead."));
        return;
    }

    m_camera = new QCamera(QMediaDevices::defaultVideoInput(), this);
    if (!m_camera->isAvailable()) {
        m_videoWidget->hide();
        m_statusLabel->setText(QStringLiteral("Camera is not available. You can still choose an "
                                              "image file instead."));
        return;
    }

    m_captureSession = new QMediaCaptureSession(this);
    m_captureSession->setCamera(m_camera);
    m_captureSession->setVideoSink(m_videoWidget->videoSink());

    m_imageCapture = new QImageCapture(this);
    m_captureSession->setImageCapture(m_imageCapture);

    connect(m_imageCapture, &QImageCapture::imageCaptured, this,
            [this](int, const QImage &image) { setCapturedImage(image); });
    connect(m_imageCapture, &QImageCapture::errorOccurred, this,
            [this](int, QImageCapture::Error, const QString &errorString) {
                m_statusLabel->setText(QStringLiteral("Capture error: %1").arg(errorString));
            });
    connect(m_camera, &QCamera::errorOccurred, this,
            [this](QCamera::Error, const QString &errorString) {
                m_statusLabel->setText(QStringLiteral("Camera error: %1").arg(errorString));
                m_captureButton->setEnabled(false);
            });

    m_captureButton->setEnabled(true);
    m_statusLabel->setText(QStringLiteral("Point the camera at the object and press Capture, or "
                                          "choose an image file."));
    m_camera->start();
}

CameraDialog::~CameraDialog()
{
    if (m_camera) {
        m_camera->stop();
    }
}

QImage CameraDialog::image() const
{
    return m_capturedImage;
}

void CameraDialog::onCapture()
{
    if (m_imageCapture) {
        m_imageCapture->capture();
    }
}

void CameraDialog::onChooseFile()
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
        m_statusLabel->setText(QStringLiteral("Could not load the selected image."));
        return;
    }
    setCapturedImage(image);
}

void CameraDialog::setCapturedImage(const QImage &image)
{
    m_capturedImage = image;

    QPixmap preview = QPixmap::fromImage(image).scaled(
        m_previewLabel->width(), m_previewLabel->height(),
        Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_previewLabel->setPixmap(preview);
    m_previewLabel->setText(QString());

    m_statusLabel->setText(QStringLiteral("Image captured. Click 'Use Image' to continue."));
    m_okButton->setEnabled(true);
}
