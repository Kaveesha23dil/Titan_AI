#ifndef TITANAI_CAMERA_DIALOG_HPP
#define TITANAI_CAMERA_DIALOG_HPP

#include <QDialog>
#include <QImage>

class QCamera;
class QImageCapture;
class QLabel;
class QMediaCaptureSession;
class QPushButton;
class QVideoWidget;

// Dialog that shows a live camera preview, lets the user capture a still image
// (or pick an image file), and returns it on accept for image analysis.
class CameraDialog : public QDialog {
    Q_OBJECT

public:
    explicit CameraDialog(QWidget *parent = nullptr);
    ~CameraDialog() override;

    [[nodiscard]] QImage image() const;

private slots:
    void onCapture();
    void onChooseFile();

private:
    void setCapturedImage(const QImage &image);

    QCamera *m_camera{nullptr};
    QMediaCaptureSession *m_captureSession{nullptr};
    QImageCapture *m_imageCapture{nullptr};
    QVideoWidget *m_videoWidget{nullptr};
    QLabel *m_previewLabel{nullptr};
    QLabel *m_statusLabel{nullptr};
    QPushButton *m_captureButton{nullptr};
    QPushButton *m_okButton{nullptr};
    QImage m_capturedImage;
};

#endif // TITANAI_CAMERA_DIALOG_HPP
