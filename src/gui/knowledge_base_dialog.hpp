#ifndef TITANAI_KNOWLEDGE_BASE_DIALOG_HPP
#define TITANAI_KNOWLEDGE_BASE_DIALOG_HPP

#include <QDialog>
#include <QString>

class Agent;

class QCheckBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;

/**
 * @brief Manage the local RAG knowledge base.
 *
 * Lets the user add folders/files, remove or clear sources, reindex, watch
 * indexing progress, and toggle automatic grounding of chat answers.
 */
class KnowledgeBaseDialog : public QDialog {
    Q_OBJECT

public:
    explicit KnowledgeBaseDialog(Agent *agent, QWidget *parent = nullptr);
    ~KnowledgeBaseDialog() override = default;

private slots:
    void addFolder();
    void addFiles();
    void removeSelected();
    void reindexAll();
    void clearIndex();
    void refreshStats();
    void onIndexingStarted(int fileCount);
    void onIndexingProgress(int processed, int total, const QString &currentFile);
    void onIndexingFinished(int filesIndexed, int chunksIndexed);
    void onIndexingError(const QString &error);

private:
    void setupUi();
    void setBusy(bool busy);

    Agent *m_agent{nullptr};
    QLabel *m_statsLabel{nullptr};
    QLabel *m_statusLabel{nullptr};
    QListWidget *m_sourceList{nullptr};
    QProgressBar *m_progressBar{nullptr};
    QCheckBox *m_groundingCheck{nullptr};
    QPushButton *m_addFolderButton{nullptr};
    QPushButton *m_addFilesButton{nullptr};
    QPushButton *m_removeButton{nullptr};
    QPushButton *m_reindexButton{nullptr};
    QPushButton *m_clearButton{nullptr};
};

#endif // TITANAI_KNOWLEDGE_BASE_DIALOG_HPP
