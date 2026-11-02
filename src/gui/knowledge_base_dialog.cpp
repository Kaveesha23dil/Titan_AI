#include "gui/knowledge_base_dialog.hpp"

#include "agent/agent.hpp"

#include <QCheckBox>
#include <QColor>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {

constexpr auto BgDeep = "#0b0f19";
constexpr auto BgCard = "#111827";
constexpr auto Border = "#1e293b";
constexpr auto Accent = "#6366f1";
constexpr auto AccentGlow = "#818cf8";
constexpr auto TextPrimary = "#f1f5f9";
constexpr auto TextSecondary = "#94a3b8";
constexpr auto TextMuted = "#64748b";
constexpr auto Success = "#22c55e";
constexpr auto Danger = "#ef4444";

QString actionButtonStyle()
{
    return QStringLiteral(
        "QPushButton { background: %1; color: white; border: none; font-weight: 600; "
        "             padding: 8px 16px; border-radius: 7px; } "
        "QPushButton:hover { background: %2; } "
        "QPushButton:disabled { background: %3; color: %4; }")
        .arg(Accent, AccentGlow, BgCard, TextMuted);
}

QString secondaryButtonStyle()
{
    return QStringLiteral(
        "QPushButton { background: %1; color: %2; border: 1px solid %3; "
        "             padding: 8px 14px; border-radius: 7px; } "
        "QPushButton:hover { border-color: %4; color: %5; } "
        "QPushButton:disabled { color: %6; }")
        .arg(BgCard, TextSecondary, Border, AccentGlow, TextPrimary, TextMuted);
}

} // namespace

KnowledgeBaseDialog::KnowledgeBaseDialog(Agent *agent, QWidget *parent)
    : QDialog(parent)
    , m_agent(agent)
{
    setWindowTitle(QStringLiteral("Local Knowledge Base (RAG)"));
    resize(760, 580);
    setupUi();

    RagManager &manager = m_agent->ragManager();
    connect(&manager, &RagManager::indexingStarted,
            this, &KnowledgeBaseDialog::onIndexingStarted);
    connect(&manager, &RagManager::indexingProgress,
            this, &KnowledgeBaseDialog::onIndexingProgress);
    connect(&manager, &RagManager::indexingFinished,
            this, &KnowledgeBaseDialog::onIndexingFinished);
    connect(&manager, &RagManager::indexingError,
            this, &KnowledgeBaseDialog::onIndexingError);
    connect(&manager, &RagManager::indexChanged,
            this, &KnowledgeBaseDialog::refreshStats);

    refreshStats();
}

void KnowledgeBaseDialog::setupUi()
{
    setStyleSheet(QStringLiteral(
        "QDialog { background: %1; } "
        "QLabel { color: %2; } "
        "QListWidget { background: %3; color: %2; border: 1px solid %4; "
        "              border-radius: 8px; padding: 6px; } "
        "QListWidget::item { padding: 7px 6px; border-bottom: 1px solid %4; } "
        "QListWidget::item:selected { background: rgba(99,102,241,0.25); border-radius: 5px; } "
        "QCheckBox { color: %5; } "
        "QProgressBar { background: %3; border: 1px solid %4; border-radius: 5px; "
        "               text-align: center; color: %2; height: 16px; } "
        "QProgressBar::chunk { background: %6; border-radius: 4px; }")
        .arg(BgDeep, TextPrimary, BgCard, Border, TextSecondary, Accent));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(14);

    auto *title = new QLabel(QStringLiteral("📚  Local Knowledge Base"), this);
    title->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 18px; font-weight: 700; }").arg(TextPrimary));
    layout->addWidget(title);

    auto *description = new QLabel(
        QStringLiteral(
            "Index PDFs, notes and source code into a private, on-device knowledge base. "
            "Answers are grounded in these documents using Ollama embeddings — nothing "
            "is uploaded."),
        this);
    description->setWordWrap(true);
    description->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 12px; }").arg(TextSecondary));
    layout->addWidget(description);

    auto *infoRow = new QHBoxLayout;
    m_statsLabel = new QLabel(this);
    m_statsLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 12px; font-weight: 600; }").arg(AccentGlow));
    infoRow->addWidget(m_statsLabel);
    infoRow->addStretch(1);

    auto *modelLabel = new QLabel(
        QStringLiteral("Embedding model: %1 (use TITANAI_EMBED_MODEL to override)").arg(m_agent->ragManager().embeddingModel()), this);
    modelLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; }").arg(TextMuted));
    infoRow->addWidget(modelLabel);
    layout->addLayout(infoRow);

    m_sourceList = new QListWidget(this);
    m_sourceList->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_sourceList, 1);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(false);
    layout->addWidget(m_progressBar);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 12px; }").arg(TextMuted));
    layout->addWidget(m_statusLabel);

    m_groundingCheck = new QCheckBox(
        QStringLiteral("Ground normal chat answers in these documents"), this);
    m_groundingCheck->setChecked(m_agent->ragAutoGround());
    connect(m_groundingCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_agent->setRagAutoGround(checked);
        QSettings settings(QStringLiteral("TitanAI"), QStringLiteral("TitanAI"));
        settings.setValue(QStringLiteral("ragAutoGround"), checked);
    });
    layout->addWidget(m_groundingCheck);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(10);

    m_addFolderButton = new QPushButton(QStringLiteral("Add Folder…"), this);
    m_addFilesButton = new QPushButton(QStringLiteral("Add Files…"), this);
    m_removeButton = new QPushButton(QStringLiteral("Remove Selected"), this);
    m_reindexButton = new QPushButton(QStringLiteral("Reindex All"), this);
    m_clearButton = new QPushButton(QStringLiteral("Clear Index"), this);

    m_addFolderButton->setStyleSheet(actionButtonStyle());
    m_addFilesButton->setStyleSheet(actionButtonStyle());
    for (QPushButton *button : {m_removeButton, m_reindexButton, m_clearButton}) {
        button->setStyleSheet(secondaryButtonStyle());
    }

    buttonRow->addWidget(m_addFolderButton);
    buttonRow->addWidget(m_addFilesButton);
    buttonRow->addWidget(m_removeButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_reindexButton);
    buttonRow->addWidget(m_clearButton);
    layout->addLayout(buttonRow);

    auto *closeRow = new QHBoxLayout;
    closeRow->addStretch(1);
    auto *closeButton = new QPushButton(QStringLiteral("Close"), this);
    closeButton->setStyleSheet(secondaryButtonStyle());
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    closeRow->addWidget(closeButton);
    layout->addLayout(closeRow);

    connect(m_addFolderButton, &QPushButton::clicked, this, &KnowledgeBaseDialog::addFolder);
    connect(m_addFilesButton, &QPushButton::clicked, this, &KnowledgeBaseDialog::addFiles);
    connect(m_removeButton, &QPushButton::clicked, this, &KnowledgeBaseDialog::removeSelected);
    connect(m_reindexButton, &QPushButton::clicked, this, &KnowledgeBaseDialog::reindexAll);
    connect(m_clearButton, &QPushButton::clicked, this, &KnowledgeBaseDialog::clearIndex);
    connect(m_sourceList, &QListWidget::itemSelectionChanged, this, [this]() {
        m_removeButton->setEnabled(m_sourceList->currentItem() != nullptr
                                   && !m_agent->ragManager().isIndexing());
    });
}

void KnowledgeBaseDialog::refreshStats()
{
    RagManager &manager = m_agent->ragManager();

    m_statsLabel->setText(QStringLiteral("%1 document(s) · %2 chunk(s)")
                              .arg(manager.documentCount())
                              .arg(manager.chunkCount()));

    m_sourceList->clear();
    for (const QString &source : manager.sources()) {
        const QString name = QFileInfo(source).fileName();
        auto *item = new QListWidgetItem(
            QStringLiteral("%1   ·   %2 chunks").arg(name).arg(manager.chunkCountForSource(source)),
            m_sourceList);
        item->setData(Qt::UserRole, source);
        item->setToolTip(source);
    }

    if (m_sourceList->count() == 0) {
        auto *empty = new QListWidgetItem(
            QStringLiteral("No documents indexed yet. Use “Add Folder…” to begin."),
            m_sourceList);
        empty->setFlags(Qt::NoItemFlags);
        empty->setForeground(QColor(TextMuted));
    }

    m_removeButton->setEnabled(m_sourceList->currentItem() != nullptr
                               && (m_sourceList->currentItem()->flags() & Qt::ItemIsSelectable)
                               && !manager.isIndexing());
    m_clearButton->setEnabled(manager.chunkCount() > 0 && !manager.isIndexing());
    m_reindexButton->setEnabled(manager.documentCount() > 0 && !manager.isIndexing());
    m_addFolderButton->setEnabled(!manager.isIndexing());
    m_addFilesButton->setEnabled(!manager.isIndexing());
}

void KnowledgeBaseDialog::setBusy(bool busy)
{
    m_addFolderButton->setEnabled(!busy);
    m_addFilesButton->setEnabled(!busy);
    m_removeButton->setEnabled(!busy && m_sourceList->currentItem() != nullptr);
    m_reindexButton->setEnabled(!busy && m_agent->ragManager().documentCount() > 0);
    m_clearButton->setEnabled(!busy && m_agent->ragManager().chunkCount() > 0);
    m_progressBar->setVisible(busy);
}

void KnowledgeBaseDialog::addFolder()
{
    const QString start = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Choose a folder to index"), start);
    if (directory.isEmpty()) {
        return;
    }
    m_agent->ragManager().indexPaths({directory});
}

void KnowledgeBaseDialog::addFiles()
{
    const QString start = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Choose documents to index"), start,
        QStringLiteral("Documents (*.pdf *.md *.markdown *.txt *.rst *.org);;"
                       "Source code (*.cpp *.h *.hpp *.c *.py *.js *.ts *.java *.go *.rs);;"
                       "All files (*)"));
    if (files.isEmpty()) {
        return;
    }
    m_agent->ragManager().indexPaths(files);
}

void KnowledgeBaseDialog::removeSelected()
{
    QListWidgetItem *item = m_sourceList->currentItem();
    if (!item) {
        return;
    }
    const QString source = item->data(Qt::UserRole).toString();
    if (source.isEmpty()) {
        return;
    }
    m_agent->ragManager().removeSource(source);
    m_statusLabel->setText(QStringLiteral("Removed %1").arg(source));
}

void KnowledgeBaseDialog::reindexAll()
{
    const QStringList sources = m_agent->ragManager().sources();
    if (sources.isEmpty()) {
        return;
    }
    m_agent->ragManager().indexPaths(sources, true);
}

void KnowledgeBaseDialog::clearIndex()
{
    if (m_agent->ragManager().chunkCount() == 0) {
        return;
    }
    const auto choice = QMessageBox::question(
        this, QStringLiteral("Clear Knowledge Base"),
        QStringLiteral("Remove all indexed documents from the local knowledge base?\n"
                       "Your original files are not affected."));
    if (choice != QMessageBox::Yes) {
        return;
    }
    m_agent->ragManager().clear();
    m_statusLabel->setText(QStringLiteral("Knowledge base cleared."));
}

void KnowledgeBaseDialog::onIndexingStarted(int fileCount)
{
    setBusy(true);
    m_progressBar->setRange(0, qMax(1, fileCount));
    m_progressBar->setValue(0);
    m_statusLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 12px; }").arg(TextMuted));
    m_statusLabel->setText(
        QStringLiteral("Indexing %1 file(s)… this runs locally and may take a while.")
            .arg(fileCount));
}

void KnowledgeBaseDialog::onIndexingProgress(int processed, int total, const QString &currentFile)
{
    m_progressBar->setRange(0, qMax(1, total));
    m_progressBar->setValue(processed);
    if (!currentFile.isEmpty()) {
        m_statusLabel->setText(
            QStringLiteral("Indexing %1 / %2 · %3")
                .arg(processed)
                .arg(total)
                .arg(QFileInfo(currentFile).fileName()));
    }
}

void KnowledgeBaseDialog::onIndexingFinished(int filesIndexed, int chunksIndexed)
{
    setBusy(false);
    m_progressBar->setVisible(false);
    m_statusLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 12px; }").arg(Success));
    m_statusLabel->setText(
        QStringLiteral("✓ Indexed %1 file(s) into %2 chunk(s).")
            .arg(filesIndexed)
            .arg(chunksIndexed));
    refreshStats();
}

void KnowledgeBaseDialog::onIndexingError(const QString &error)
{
    setBusy(false);
    m_progressBar->setVisible(false);
    m_statusLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 12px; }").arg(Danger));
    m_statusLabel->setText(QStringLiteral("❌ %1").arg(error));
    refreshStats();
}
