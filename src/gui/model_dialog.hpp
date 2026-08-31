#ifndef TITANAI_MODEL_DIALOG_HPP
#define TITANAI_MODEL_DIALOG_HPP

#include <QDialog>
#include <QStringList>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QPushButton;

// Modal dialog for switching the active Ollama model. Shows every model
// currently installed locally (fetched from the Ollama server), lets the
// user pick one to become active, and offers a curated list of more powerful
// models that can be pulled by name for high-end machines.
class ModelDialog : public QDialog {
    Q_OBJECT

public:
    explicit ModelDialog(const QString &currentModel,
                         const QStringList &installedModels,
                         QWidget *parent = nullptr);

    [[nodiscard]] QString selectedModel() const;
    void setInstalledModels(const QStringList &models);

signals:
    void refreshRequested();

private slots:
    void onSwitchModel();
    void onRefresh();
    void onPullModel();
    void onInstalledItemChanged();
    void onSuggestedItemChanged();

private:
    void populateInstalledList();
    void populateSuggestedList();
    QString suggestedTag(const QListWidgetItem *item) const;
    void setBusy(bool busy);

    QString m_currentModel;
    QStringList m_installedModels;

    QListWidget *m_installedList{nullptr};
    QListWidget *m_suggestedList{nullptr};
    QLineEdit *m_pullEdit{nullptr};
    QPushButton *m_switchButton{nullptr};
    QLabel *m_statusLabel{nullptr};
    QProgressBar *m_progressBar{nullptr};
};

#endif // TITANAI_MODEL_DIALOG_HPP
