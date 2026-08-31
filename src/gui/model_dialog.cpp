#include "gui/model_dialog.hpp"

#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr auto kBgCard      = "#111827";
constexpr auto kBgInput     = "#0f1629";
constexpr auto kBorder      = "#1e293b";
constexpr auto kAccent      = "#6366f1";
constexpr auto kAccentGlow  = "#818cf8";
constexpr auto kTextPrimary = "#f1f5f9";
constexpr auto kTextSecondary = "#94a3b8";
constexpr auto kTextMuted   = "#64748b";
constexpr auto kSuccess     = "#22c55e";
constexpr auto kWarning     = "#f59e0b";

// Curated list of more powerful Ollama models suited to high-end machines.
// Format: display name, tag, recommended RAM/VRAM.
void addSuggestedModels(QListWidget *list)
{
    struct Suggestion { const char *label; const char *tag; const char *note; };
    static const Suggestion suggestions[] = {
        {"🧠 Qwen2.5 Coder 14B", "qwen2.5-coder:14b", "~9 GB · strong coding"},
        {"🚀 Qwen2.5 Coder 32B", "qwen2.5-coder:32b", "~20 GB · advanced coding"},
        {"🔭 DeepSeek Coder 33B", "deepseek-coder:33b", "~20 GB · deep reasoning"},
        {"🌐 Command-R 35B", "command-r:35b", "~20 GB · general assistant"},
        {"✨ Llama 3.3 70B", "llama3.3:70b", "~40 GB · flagship"},
        {"🖼️ Gemma 3 Vision 12B", "gemma3:12b", "~8 GB · vision capable"},
        {"⚡ Gemma 3 Vision 27B", "gemma3:27b", "~17 GB · vision capable"},
        {"🔥 Qwen2.5 72B", "qwen2.5:72b", "~49 GB · maximal"},
    };

    for (const Suggestion &s : suggestions) {
        auto *item = new QListWidgetItem(QStringLiteral("%1  —  %2").arg(QLatin1String(s.label), QLatin1String(s.note)));
        item->setData(Qt::UserRole, QLatin1String(s.tag));
        list->addItem(item);
    }
}

} // namespace

ModelDialog::ModelDialog(const QString &currentModel,
                         const QStringList &installedModels,
                         QWidget *parent)
    : QDialog(parent)
    , m_currentModel(currentModel)
    , m_installedModels(installedModels)
{
    setWindowTitle(QStringLiteral("AI Model Manager"));
    resize(640, 600);
    setStyleSheet(QStringLiteral("QDialog { background: #0b0f19; }"));

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("Switch or install an Ollama model"), this);
    title->setStyleSheet(QStringLiteral("color: %1; font-size: 16px; font-weight: 700;").arg(kTextPrimary));
    mainLayout->addWidget(title);

    auto *currentLabel = new QLabel(this);
    currentLabel->setTextFormat(Qt::RichText);
    currentLabel->setText(QStringLiteral("Active model: <b style='color:%1'>%2</b>")
                              .arg(kSuccess, m_currentModel));
    currentLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 13px;").arg(kTextSecondary));
    mainLayout->addWidget(currentLabel);

    // ── Installed models ────────────────────────────────────
    auto *installedHeader = new QHBoxLayout;
    auto *installedTitle = new QLabel(QStringLiteral("Installed models"), this);
    installedTitle->setStyleSheet(QStringLiteral("color: %1; font-size: 13px; font-weight: 600;").arg(kTextSecondary));
    auto *refreshBtn = new QPushButton(QStringLiteral("⟳ Refresh"), this);
    refreshBtn->setCursor(Qt::PointingHandCursor);
    refreshBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background:%1; border:1px solid %2; border-radius:6px; color:%3; padding:4px 12px; }"
        "QPushButton:hover { background:%4; }").arg(kBgCard, kBorder, kTextSecondary, kAccent));
    installedHeader->addWidget(installedTitle);
    installedHeader->addStretch(1);
    installedHeader->addWidget(refreshBtn);
    mainLayout->addLayout(installedHeader);

    m_installedList = new QListWidget(this);
    m_installedList->setStyleSheet(QStringLiteral(
        "QListWidget { background:%1; border:1px solid %2; border-radius:8px; color:%3; padding:4px; }"
        "QListWidget::item { padding:6px; border-radius:4px; }"
        "QListWidget::item:selected { background:%4; color:white; }").arg(kBgCard, kBorder, kTextPrimary, kAccent));
    mainLayout->addWidget(m_installedList, 1);
    populateInstalledList();

    // ── Suggested powerful models ───────────────────────────
    auto *suggestedTitle = new QLabel(QStringLiteral("Install a more powerful model (high-end machines)"), this);
    suggestedTitle->setStyleSheet(QStringLiteral("color: %1; font-size: 13px; font-weight: 600;").arg(kTextSecondary));
    mainLayout->addWidget(suggestedTitle);

    m_suggestedList = new QListWidget(this);
    m_suggestedList->setStyleSheet(QStringLiteral(
        "QListWidget { background:%1; border:1px solid %2; border-radius:8px; color:%3; padding:4px; }"
        "QListWidget::item { padding:6px; border-radius:4px; }"
        "QListWidget::item:selected { background:%4; color:white; }").arg(kBgCard, kBorder, kTextPrimary, kAccent));
    mainLayout->addWidget(m_suggestedList, 1);
    populateSuggestedList();

    // ── Pull custom model ───────────────────────────────────
    auto *pullRow = new QHBoxLayout;
    m_pullEdit = new QLineEdit(this);
    m_pullEdit->setPlaceholderText(QStringLiteral("Enter model tag to pull, e.g. qwen2.5-coder:14b"));
    m_pullEdit->setStyleSheet(QStringLiteral(
        "QLineEdit { background:%1; border:1px solid %2; border-radius:6px; color:%3; padding:8px 10px; }").arg(kBgInput, kBorder, kTextPrimary));
    auto *pullBtn = new QPushButton(QStringLiteral("⬇ Pull"), this);
    pullBtn->setCursor(Qt::PointingHandCursor);
    pullBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background:%1; color:white; border:none; border-radius:6px; padding:8px 16px; font-weight:600; }"
        "QPushButton:hover { background:%2; }").arg(kAccent, kAccentGlow));
    pullRow->addWidget(m_pullEdit, 1);
    pullRow->addWidget(pullBtn);
    mainLayout->addLayout(pullRow);

    // ── Status / progress ───────────────────────────────────
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(kTextMuted));
    mainLayout->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(8);
    m_progressBar->setStyleSheet(QStringLiteral(
        "QProgressBar { background:%1; border:none; border-radius:4px; }"
        "QProgressBar::chunk { background:%2; border-radius:4px; }").arg(kBorder, kAccent));
    m_progressBar->hide();
    mainLayout->addWidget(m_progressBar);

    // ── Buttons ─────────────────────────────────────────────
    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    m_switchButton = new QPushButton(QStringLiteral("Switch to selected"), this);
    m_switchButton->setCursor(Qt::PointingHandCursor);
    m_switchButton->setStyleSheet(QStringLiteral(
        "QPushButton { background:%1; color:white; border:none; border-radius:6px; padding:8px 16px; font-weight:600; }"
        "QPushButton:hover { background:%2; }"
        "QPushButton:disabled { background:%3; color:%4; }").arg(kAccent, kAccentGlow, kBorder, kTextMuted));
    auto *closeBtn = new QPushButton(QStringLiteral("Close"), this);
    closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background:%1; border:1px solid %2; border-radius:6px; color:%3; padding:6px 16px; }"
        "QPushButton:hover { background:%4; }").arg(kBgCard, kBorder, kTextSecondary, kAccent));
    buttonRow->addWidget(m_switchButton);
    buttonRow->addWidget(closeBtn);
    mainLayout->addLayout(buttonRow);

    connect(m_switchButton, &QPushButton::clicked, this, &ModelDialog::onSwitchModel);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(refreshBtn, &QPushButton::clicked, this, &ModelDialog::onRefresh);
    connect(pullBtn, &QPushButton::clicked, this, &ModelDialog::onPullModel);
    connect(m_installedList, &QListWidget::itemSelectionChanged, this, &ModelDialog::onInstalledItemChanged);
    connect(m_suggestedList, &QListWidget::itemSelectionChanged, this, &ModelDialog::onSuggestedItemChanged);

    onInstalledItemChanged();
}

QString ModelDialog::selectedModel() const
{
    QListWidgetItem *item = m_installedList->currentItem();
    if (item) {
        return item->data(Qt::UserRole).toString();
    }
    return QString();
}

void ModelDialog::setInstalledModels(const QStringList &models)
{
    m_installedModels = models;
    m_progressBar->hide();
    populateInstalledList();

    // Keep the current model highlighted if still present.
    for (int i = 0; i < m_installedList->count(); ++i) {
        QListWidgetItem *item = m_installedList->item(i);
        if (item->data(Qt::UserRole).toString() == m_currentModel) {
            m_installedList->setCurrentItem(item);
            break;
        }
    }
    onInstalledItemChanged();
}

void ModelDialog::populateInstalledList()
{
    m_installedList->clear();
    if (m_installedModels.isEmpty()) {
        auto *empty = new QListWidgetItem(QStringLiteral("No models installed yet. Pick one below or enter a tag to pull."));
        empty->setForeground(QColor(kTextMuted));
        empty->setFlags(Qt::NoItemFlags);
        m_installedList->addItem(empty);
        return;
    }

    for (const QString &model : m_installedModels) {
        auto *item = new QListWidgetItem(model);
        item->setData(Qt::UserRole, model);
        if (model == m_currentModel) {
            item->setText(QStringLiteral("★ %1  (active)").arg(model));
            item->setForeground(QColor(kSuccess));
        }
        m_installedList->addItem(item);
    }

    m_statusLabel->setText(QStringLiteral("Installed: %1").arg(m_installedModels.size()));
}

void ModelDialog::populateSuggestedList()
{
    m_suggestedList->clear();
    addSuggestedModels(m_suggestedList);
}

QString ModelDialog::suggestedTag(const QListWidgetItem *item) const
{
    if (!item) {
        return QString();
    }
    return item->data(Qt::UserRole).toString();
}

void ModelDialog::onInstalledItemChanged()
{
    m_suggestedList->clearSelection();
    QListWidgetItem *item = m_installedList->currentItem();
    const bool valid = item != nullptr && !item->data(Qt::UserRole).toString().isEmpty();
    m_switchButton->setEnabled(valid);
}

void ModelDialog::onSuggestedItemChanged()
{
    m_installedList->clearSelection();
    const QString tag = suggestedTag(m_suggestedList->currentItem());
    m_pullEdit->setText(tag);
    m_pullEdit->setFocus();
    m_switchButton->setEnabled(false);
}

void ModelDialog::onSwitchModel()
{
    const QString model = selectedModel();
    if (!model.isEmpty()) {
        accept();
    }
}

void ModelDialog::onRefresh()
{
    m_statusLabel->setText(QStringLiteral("Fetching installed models..."));
    m_progressBar->show();
    emit refreshRequested();
}

void ModelDialog::onPullModel()
{
    const QString tag = m_pullEdit->text().trimmed();
    if (tag.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("Enter a model tag to pull first."));
        return;
    }

    static const QRegularExpression kTagRe(QStringLiteral("^[A-Za-z0-9._:-]+$"));
    if (!kTagRe.match(tag).hasMatch()) {
        m_statusLabel->setText(QStringLiteral("Invalid model tag. Use format like qwen2.5-coder:14b"));
        return;
    }

    m_pullEdit->setText(tag);
    m_statusLabel->setText(QStringLiteral("Run this in a terminal to install:  ollama pull %1").arg(tag));
}

void ModelDialog::setBusy(bool busy)
{
    m_switchButton->setEnabled(!busy);
    m_progressBar->setVisible(busy);
    if (!busy) {
        m_statusLabel->setText(QStringLiteral("Installed: %1").arg(m_installedModels.size()));
    }
}
