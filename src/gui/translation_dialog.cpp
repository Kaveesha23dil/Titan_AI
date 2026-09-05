#include "gui/translation_dialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

// ─────────────────────────────────────────────────────────────
//  Palette tokens (kept in sync with MainWindow Col:: namespace)
// ─────────────────────────────────────────────────────────────
namespace TC {
constexpr auto BgDeep      = "#0b0f19";
constexpr auto BgCard      = "#111827";
constexpr auto BgCardHover = "#1a2236";
constexpr auto BgInput     = "#0f1629";
constexpr auto Border      = "#1e293b";
constexpr auto BorderLight = "#2a3a52";
constexpr auto Accent      = "#6366f1";
constexpr auto AccentGlow  = "#818cf8";
constexpr auto AccentCyan  = "#22d3ee";
constexpr auto AccentGreen = "#22c55e";
constexpr auto TextPrimary = "#f1f5f9";
constexpr auto TextSec     = "#94a3b8";
constexpr auto TextMuted   = "#64748b";
constexpr auto Danger      = "#ef4444";
constexpr auto Warning     = "#f59e0b";
}

// ─────────────────────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────────────────────
TranslationDialog::TranslationDialog(TranslationAssistant *assistant, QWidget *parent)
    : QDialog(parent)
    , m_assistant(assistant)
    , m_autoTimer(new QTimer(this))
{
    setWindowTitle(QStringLiteral("🌐 Translation Assistant"));
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);
    setMinimumSize(860, 620);
    resize(1100, 720);

    setupUi();
    setupStylesheet();
    setupShortcuts();
    populateLanguageCombos();
    refreshHistory();

    m_autoTimer->setSingleShot(true);
    m_autoTimer->setInterval(800);  // 800 ms debounce
    connect(m_autoTimer, &QTimer::timeout, this, &TranslationDialog::onAutoTranslateTimer);

    connect(m_assistant, &TranslationAssistant::translationReady,
            this, &TranslationDialog::onTranslationReady);
    connect(m_assistant, &TranslationAssistant::translationError,
            this, &TranslationDialog::onTranslationError);
    connect(m_assistant, &TranslationAssistant::translationProgress,
            this, &TranslationDialog::onTranslationProgress);
}

// ─────────────────────────────────────────────────────────────
//  UI Setup
// ─────────────────────────────────────────────────────────────
void TranslationDialog::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    // ── Title bar ────────────────────────────────────────────
    auto *titleRow = new QHBoxLayout;
    auto *titleLabel = new QLabel(QStringLiteral("🌐  Translation Assistant"), this);
    titleLabel->setObjectName(QStringLiteral("dlgTitle"));
    titleRow->addWidget(titleLabel);
    titleRow->addStretch();

    // Auto-translate toggle
    m_autoCheck = new QCheckBox(QStringLiteral("Auto-translate"), this);
    m_autoCheck->setChecked(true);
    m_autoCheck->setObjectName(QStringLiteral("autoCheck"));
    connect(m_autoCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_autoTranslate = on;
    });
    titleRow->addWidget(m_autoCheck);
    root->addLayout(titleRow);

    // ── Language / tone bar ──────────────────────────────────
    auto *langBar = new QHBoxLayout;
    langBar->setSpacing(8);

    m_sourceLangCombo = new QComboBox(this);
    m_sourceLangCombo->setObjectName(QStringLiteral("langCombo"));
    m_sourceLangCombo->setMinimumWidth(180);
    m_sourceLangCombo->setToolTip(QStringLiteral("Source Language"));

    m_swapButton = new QPushButton(QStringLiteral("⇄"), this);
    m_swapButton->setObjectName(QStringLiteral("swapBtn"));
    m_swapButton->setToolTip(QStringLiteral("Swap Languages"));
    m_swapButton->setFixedSize(36, 36);
    connect(m_swapButton, &QPushButton::clicked, this, &TranslationDialog::onSwapLanguages);

    m_targetLangCombo = new QComboBox(this);
    m_targetLangCombo->setObjectName(QStringLiteral("langCombo"));
    m_targetLangCombo->setMinimumWidth(180);
    m_targetLangCombo->setToolTip(QStringLiteral("Target Language"));

    m_toneCombo = new QComboBox(this);
    m_toneCombo->setObjectName(QStringLiteral("toneCombo"));
    m_toneCombo->setToolTip(QStringLiteral("Translation Tone"));
    for (const QString &t : TranslationAssistant::toneNames())
        m_toneCombo->addItem(t);

    m_grabButton = new QPushButton(QStringLiteral("📋 Grab Selection"), this);
    m_grabButton->setObjectName(QStringLiteral("grabBtn"));
    m_grabButton->setToolTip(QStringLiteral("Grab selected or clipboard text  (Ctrl+G)"));
    connect(m_grabButton, &QPushButton::clicked, this, &TranslationDialog::onGrabSelectionClicked);

    langBar->addWidget(new QLabel(QStringLiteral("From:"), this));
    langBar->addWidget(m_sourceLangCombo);
    langBar->addWidget(m_swapButton);
    langBar->addWidget(new QLabel(QStringLiteral("To:"), this));
    langBar->addWidget(m_targetLangCombo);
    langBar->addSpacing(12);
    langBar->addWidget(new QLabel(QStringLiteral("Tone:"), this));
    langBar->addWidget(m_toneCombo);
    langBar->addStretch();
    langBar->addWidget(m_grabButton);
    root->addLayout(langBar);

    // ── Main splitter (left = translate, right = history) ────
    auto *mainSplitter = new QSplitter(Qt::Horizontal, this);
    mainSplitter->setHandleWidth(6);

    // Left: source + result + breakdown
    auto *leftPanel = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);

    // Source card
    auto *sourceCard = new QWidget(this);
    sourceCard->setObjectName(QStringLiteral("card"));
    auto *sourceCardLayout = new QVBoxLayout(sourceCard);
    sourceCardLayout->setContentsMargins(12, 10, 12, 10);
    sourceCardLayout->setSpacing(6);

    auto *sourceHeader = new QHBoxLayout;
    auto *sourceTitleLbl = new QLabel(QStringLiteral("📝  Source Text"), sourceCard);
    sourceTitleLbl->setObjectName(QStringLiteral("cardTitle"));
    m_detectedLangLabel = new QLabel(QStringLiteral(""), sourceCard);
    m_detectedLangLabel->setObjectName(QStringLiteral("detectedBadge"));
    m_charCountLabel = new QLabel(QStringLiteral("0 chars"), sourceCard);
    m_charCountLabel->setObjectName(QStringLiteral("charCount"));
    sourceHeader->addWidget(sourceTitleLbl);
    sourceHeader->addWidget(m_detectedLangLabel);
    sourceHeader->addStretch();
    sourceHeader->addWidget(m_charCountLabel);
    sourceCardLayout->addLayout(sourceHeader);

    m_sourceEdit = new QTextEdit(sourceCard);
    m_sourceEdit->setObjectName(QStringLiteral("sourceEdit"));
    m_sourceEdit->setPlaceholderText(QStringLiteral("Enter text to translate…  (Ctrl+G to grab selection)"));
    m_sourceEdit->setMinimumHeight(120);
    m_sourceEdit->setMaximumHeight(200);
    connect(m_sourceEdit, &QTextEdit::textChanged, this, &TranslationDialog::onSourceTextChanged);
    sourceCardLayout->addWidget(m_sourceEdit);

    auto *sourceActions = new QHBoxLayout;
    m_clearSourceBtn = new QPushButton(QStringLiteral("✕ Clear"), sourceCard);
    m_clearSourceBtn->setObjectName(QStringLiteral("ghostBtn"));
    connect(m_clearSourceBtn, &QPushButton::clicked, this, &TranslationDialog::onClearSource);
    m_speakSourceBtn = new QPushButton(QStringLiteral("🔊 Listen"), sourceCard);
    m_speakSourceBtn->setObjectName(QStringLiteral("ghostBtn"));
    connect(m_speakSourceBtn, &QPushButton::clicked, this, &TranslationDialog::onSpeakSource);
    m_translateBtn = new QPushButton(QStringLiteral("⚡  Translate"), sourceCard);
    m_translateBtn->setObjectName(QStringLiteral("primaryBtn"));
    m_translateBtn->setDefault(false);
    connect(m_translateBtn, &QPushButton::clicked, this, &TranslationDialog::onTranslateClicked);

    sourceActions->addWidget(m_clearSourceBtn);
    sourceActions->addWidget(m_speakSourceBtn);
    sourceActions->addStretch();
    sourceActions->addWidget(m_translateBtn);
    sourceCardLayout->addLayout(sourceActions);
    leftLayout->addWidget(sourceCard);

    // Result card
    auto *resultCard = new QWidget(this);
    resultCard->setObjectName(QStringLiteral("card"));
    auto *resultCardLayout = new QVBoxLayout(resultCard);
    resultCardLayout->setContentsMargins(12, 10, 12, 10);
    resultCardLayout->setSpacing(6);

    auto *resultHeader = new QHBoxLayout;
    auto *resultTitleLbl = new QLabel(QStringLiteral("✅  Translation"), resultCard);
    resultTitleLbl->setObjectName(QStringLiteral("cardTitle"));
    m_statusLabel = new QLabel(QStringLiteral(""), resultCard);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    resultHeader->addWidget(resultTitleLbl);
    resultHeader->addStretch();
    resultHeader->addWidget(m_statusLabel);
    resultCardLayout->addLayout(resultHeader);

    m_resultBrowser = new QTextBrowser(resultCard);
    m_resultBrowser->setObjectName(QStringLiteral("resultBrowser"));
    m_resultBrowser->setOpenExternalLinks(false);
    m_resultBrowser->setMinimumHeight(120);
    resultCardLayout->addWidget(m_resultBrowser);

    auto *resultActions = new QHBoxLayout;
    m_copyBtn = new QPushButton(QStringLiteral("📋 Copy"), resultCard);
    m_copyBtn->setObjectName(QStringLiteral("ghostBtn"));
    m_copyBtn->setEnabled(false);
    connect(m_copyBtn, &QPushButton::clicked, this, &TranslationDialog::onCopyTranslation);

    m_speakResultBtn = new QPushButton(QStringLiteral("🔊 Listen"), resultCard);
    m_speakResultBtn->setObjectName(QStringLiteral("ghostBtn"));
    m_speakResultBtn->setEnabled(false);
    connect(m_speakResultBtn, &QPushButton::clicked, this, &TranslationDialog::onSpeakTranslation);

    m_favoriteBtn = new QPushButton(QStringLiteral("☆ Favorite"), resultCard);
    m_favoriteBtn->setObjectName(QStringLiteral("ghostBtn"));
    m_favoriteBtn->setEnabled(false);
    connect(m_favoriteBtn, &QPushButton::clicked, this, &TranslationDialog::onFavoriteClicked);

    m_sendToChatBtn = new QPushButton(QStringLiteral("💬 Send to Chat"), resultCard);
    m_sendToChatBtn->setObjectName(QStringLiteral("accentBtn"));
    m_sendToChatBtn->setEnabled(false);
    connect(m_sendToChatBtn, &QPushButton::clicked, this, &TranslationDialog::onSendToChat);

    resultActions->addWidget(m_copyBtn);
    resultActions->addWidget(m_speakResultBtn);
    resultActions->addWidget(m_favoriteBtn);
    resultActions->addStretch();
    resultActions->addWidget(m_sendToChatBtn);
    resultCardLayout->addLayout(resultActions);
    leftLayout->addWidget(resultCard);

    // Breakdown drawer (collapsible)
    m_toggleBreakdownBtn = new QPushButton(QStringLiteral("▶  Pronunciation & Grammar Details"), this);
    m_toggleBreakdownBtn->setObjectName(QStringLiteral("toggleDrawerBtn"));
    m_toggleBreakdownBtn->setCheckable(true);
    connect(m_toggleBreakdownBtn, &QPushButton::clicked, this, &TranslationDialog::onToggleBreakdown);
    leftLayout->addWidget(m_toggleBreakdownBtn);

    m_breakdownBrowser = new QTextBrowser(this);
    m_breakdownBrowser->setObjectName(QStringLiteral("breakdownBrowser"));
    m_breakdownBrowser->setMaximumHeight(160);
    m_breakdownBrowser->hide();
    leftLayout->addWidget(m_breakdownBrowser);

    leftLayout->addStretch();

    // Right: history panel
    auto *rightPanel = new QWidget(this);
    rightPanel->setObjectName(QStringLiteral("historyPanel"));
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(6);

    auto *historyHeader = new QHBoxLayout;
    auto *historyTitle = new QLabel(QStringLiteral("🕐  Recent Translations"), rightPanel);
    historyTitle->setObjectName(QStringLiteral("cardTitle"));
    m_clearHistoryBtn = new QPushButton(QStringLiteral("Clear"), rightPanel);
    m_clearHistoryBtn->setObjectName(QStringLiteral("ghostBtn"));
    m_clearHistoryBtn->setFixedHeight(26);
    connect(m_clearHistoryBtn, &QPushButton::clicked, this, &TranslationDialog::onClearHistory);
    historyHeader->addWidget(historyTitle);
    historyHeader->addStretch();
    historyHeader->addWidget(m_clearHistoryBtn);
    rightLayout->addLayout(historyHeader);

    m_historySearch = new QLineEdit(rightPanel);
    m_historySearch->setPlaceholderText(QStringLiteral("Search history…"));
    m_historySearch->setObjectName(QStringLiteral("historySearch"));
    connect(m_historySearch, &QLineEdit::textChanged,
            this, &TranslationDialog::onHistorySearchChanged);
    rightLayout->addWidget(m_historySearch);

    m_historyList = new QListWidget(rightPanel);
    m_historyList->setObjectName(QStringLiteral("historyList"));
    m_historyList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_historyList, &QListWidget::itemClicked,
            this, &TranslationDialog::onHistoryItemClicked);
    rightLayout->addWidget(m_historyList);

    mainSplitter->addWidget(leftPanel);
    mainSplitter->addWidget(rightPanel);
    mainSplitter->setStretchFactor(0, 3);
    mainSplitter->setStretchFactor(1, 1);
    root->addWidget(mainSplitter, 1);
}

// ─────────────────────────────────────────────────────────────
//  Stylesheet
// ─────────────────────────────────────────────────────────────
void TranslationDialog::setupStylesheet()
{
    setStyleSheet(QStringLiteral(
        "TranslationDialog { background: %1; }"

        "QLabel { color: %2; font-size: 13px; }"
        "QLabel#dlgTitle { font-size: 20px; font-weight: 700; color: %3; "
        "  background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 %3,stop:1 %4); "
        "  -webkit-background-clip: text; }"

        "QLabel#cardTitle { font-size: 13px; font-weight: 600; color: %3; }"
        "QLabel#detectedBadge { font-size: 11px; color: %5; background: %6; "
        "  border: 1px solid %3; border-radius: 10px; padding: 1px 8px; }"
        "QLabel#charCount { font-size: 11px; color: %7; }"
        "QLabel#statusLabel { font-size: 11px; color: %8; font-style: italic; }"

        "QWidget#card { background: %9; border: 1px solid %10; border-radius: 12px; }"
        "QWidget#historyPanel { background: %9; border: 1px solid %10; border-radius: 12px; "
        "  padding: 8px; }"

        "QTextEdit#sourceEdit { background: %11; color: %2; border: 1px solid %10; "
        "  border-radius: 8px; padding: 10px; font-size: 14px; selection-background-color: %3; }"
        "QTextEdit#sourceEdit:focus { border-color: %3; }"

        "QTextBrowser#resultBrowser { background: %11; color: %2; border: 1px solid %10; "
        "  border-radius: 8px; padding: 10px; font-size: 14px; }"
        "QTextBrowser#breakdownBrowser { background: %6; color: %2; border: 1px solid %10; "
        "  border-radius: 8px; padding: 10px; font-size: 13px; }"

        "QPushButton#primaryBtn { background: %3; color: white; font-weight: 600; "
        "  border-radius: 8px; padding: 8px 20px; font-size: 13px; border: none; }"
        "QPushButton#primaryBtn:hover { background: %4; }"
        "QPushButton#primaryBtn:pressed { background: %3; }"
        "QPushButton#primaryBtn:disabled { background: %7; color: %7; }"

        "QPushButton#accentBtn { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "  stop:0 %3, stop:1 %4); color: white; font-weight: 600; "
        "  border-radius: 8px; padding: 8px 20px; font-size: 13px; border: none; }"
        "QPushButton#accentBtn:hover { opacity: 0.9; }"
        "QPushButton#accentBtn:disabled { background: %7; color: %7; }"

        "QPushButton#ghostBtn { background: transparent; color: %8; font-size: 12px; "
        "  border: 1px solid %10; border-radius: 6px; padding: 4px 12px; }"
        "QPushButton#ghostBtn:hover { background: %12; color: %2; border-color: %3; }"

        "QPushButton#swapBtn { background: %12; color: %3; font-size: 18px; font-weight: 700; "
        "  border: 1px solid %3; border-radius: 18px; }"
        "QPushButton#swapBtn:hover { background: %3; color: white; }"

        "QPushButton#grabBtn { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "  stop:0 #0e7490,stop:1 #06b6d4); color: white; font-weight: 600; "
        "  border-radius: 8px; padding: 6px 14px; font-size: 12px; border: none; }"
        "QPushButton#grabBtn:hover { background: #06b6d4; }"

        "QPushButton#toggleDrawerBtn { background: %12; color: %8; text-align: left; "
        "  font-size: 12px; border: 1px solid %10; border-radius: 8px; padding: 6px 12px; }"
        "QPushButton#toggleDrawerBtn:hover { color: %3; border-color: %3; }"
        "QPushButton#toggleDrawerBtn:checked { color: %3; border-color: %3; }"

        "QComboBox#langCombo, QComboBox#toneCombo { background: %11; color: %2; "
        "  border: 1px solid %10; border-radius: 8px; padding: 6px 10px; font-size: 13px; }"
        "QComboBox#langCombo:focus, QComboBox#toneCombo:focus { border-color: %3; }"
        "QComboBox::drop-down { border: none; width: 20px; }"
        "QComboBox QAbstractItemView { background: %9; color: %2; border: 1px solid %10; "
        "  selection-background-color: %3; outline: none; }"

        "QLineEdit#historySearch { background: %11; color: %2; border: 1px solid %10; "
        "  border-radius: 6px; padding: 6px 10px; font-size: 12px; }"

        "QListWidget#historyList { background: transparent; border: none; color: %2; }"
        "QListWidget#historyList::item { background: %12; border-radius: 6px; padding: 6px 8px; "
        "  margin-bottom: 3px; }"
        "QListWidget#historyList::item:hover { background: %13; border: 1px solid %3; }"
        "QListWidget#historyList::item:selected { background: %3; color: white; }"

        "QCheckBox#autoCheck { color: %8; font-size: 12px; spacing: 6px; }"
        "QCheckBox#autoCheck::indicator { width: 16px; height: 16px; border-radius: 4px; "
        "  border: 1px solid %10; background: %11; }"
        "QCheckBox#autoCheck::indicator:checked { background: %3; border-color: %3; }"

        "QSplitter::handle { background: %10; }"

        "QScrollBar:vertical { background: %11; width: 6px; border-radius: 3px; }"
        "QScrollBar::handle:vertical { background: %7; border-radius: 3px; min-height: 20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    )
    .arg(QLatin1String(TC::BgDeep))     // 1 - dialog bg
    .arg(QLatin1String(TC::TextPrimary))// 2 - text
    .arg(QLatin1String(TC::Accent))     // 3 - accent indigo
    .arg(QLatin1String(TC::AccentGlow)) // 4 - accent glow
    .arg(QLatin1String(TC::AccentCyan)) // 5 - badge text
    .arg(QStringLiteral("#0e1a2e"))     // 6 - badge bg / breakdown bg
    .arg(QLatin1String(TC::TextMuted))  // 7 - muted
    .arg(QLatin1String(TC::TextSec))    // 8 - secondary text
    .arg(QLatin1String(TC::BgCard))     // 9 - card bg
    .arg(QLatin1String(TC::Border))     // 10 - border
    .arg(QLatin1String(TC::BgInput))    // 11 - input bg
    .arg(QLatin1String(TC::BgCardHover))// 12 - hover bg
    .arg(QStringLiteral("#1e293b"))     // 13 - list hover
    );
}

// ─────────────────────────────────────────────────────────────
//  Shortcuts
// ─────────────────────────────────────────────────────────────
void TranslationDialog::setupShortcuts()
{
    // Ctrl+Enter → Translate
    auto *shortcutTranslate = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(shortcutTranslate, &QShortcut::activated, this, &TranslationDialog::onTranslateClicked);

    // Ctrl+G → Grab selection
    auto *shortcutGrab = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_G), this);
    connect(shortcutGrab, &QShortcut::activated, this, &TranslationDialog::onGrabSelectionClicked);

    // Ctrl+Shift+C → Copy translation
    auto *shortcutCopy = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C), this);
    connect(shortcutCopy, &QShortcut::activated, this, &TranslationDialog::onCopyTranslation);

    // Esc → Close
    auto *shortcutClose = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(shortcutClose, &QShortcut::activated, this, &QDialog::close);
}

// ─────────────────────────────────────────────────────────────
//  Populate Combos
// ─────────────────────────────────────────────────────────────
void TranslationDialog::populateLanguageCombos()
{
    const QList<LanguageInfo> langs = TranslationAssistant::supportedLanguages();

    for (const LanguageInfo &lang : langs) {
        const QString display = QStringLiteral("%1  %2").arg(lang.flag, lang.name);
        m_sourceLangCombo->addItem(display, lang.code);
    }
    // Source defaults to "auto"
    m_sourceLangCombo->setCurrentIndex(0);

    for (const LanguageInfo &lang : langs) {
        if (lang.code == QLatin1String("auto")) continue;
        const QString display = QStringLiteral("%1  %2").arg(lang.flag, lang.name);
        m_targetLangCombo->addItem(display, lang.code);
    }
    // Default target = English
    const int enIdx = m_targetLangCombo->findData(QStringLiteral("en"));
    m_targetLangCombo->setCurrentIndex(enIdx >= 0 ? enIdx : 0);
}

// ─────────────────────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────────────────────
void TranslationDialog::setSourceText(const QString &text)
{
    m_sourceEdit->setPlainText(text);
    m_sourceEdit->moveCursor(QTextCursor::End);
    if (m_autoTranslate && !text.trimmed().isEmpty())
        onTranslateClicked();
}

void TranslationDialog::setTargetLanguage(const QString &langCode)
{
    const int idx = m_targetLangCombo->findData(langCode);
    if (idx >= 0)
        m_targetLangCombo->setCurrentIndex(idx);
}

// ─────────────────────────────────────────────────────────────
//  Slots
// ─────────────────────────────────────────────────────────────
void TranslationDialog::onSourceTextChanged()
{
    const int len = m_sourceEdit->toPlainText().length();
    m_charCountLabel->setText(QStringLiteral("%1 chars").arg(len));

    // Heuristic language detection for badge
    if (len > 4) {
        const QString detected = TranslationAssistant::detectLanguage(m_sourceEdit->toPlainText());
        const LanguageInfo info = TranslationAssistant::languageByCode(detected);
        if (!info.name.isEmpty())
            m_detectedLangLabel->setText(QStringLiteral("%1 %2").arg(info.flag, info.name));
    } else {
        m_detectedLangLabel->clear();
    }

    if (m_autoTranslate && len > 2)
        m_autoTimer->start();
}

void TranslationDialog::onAutoTranslateTimer()
{
    onTranslateClicked();
}

void TranslationDialog::onTranslateClicked()
{
    const QString text = m_sourceEdit->toPlainText().trimmed();
    if (text.isEmpty()) return;

    TranslationRequest req;
    req.text             = text;
    req.sourceLanguage   = m_sourceLangCombo->currentData().toString();
    req.targetLanguage   = m_targetLangCombo->currentData().toString();
    req.tone             = TranslationAssistant::toneFromName(m_toneCombo->currentText());
    req.provideBreakdown = true;
    req.providePronunciation = true;

    setTranslating(true);
    m_assistant->translate(req);
}

void TranslationDialog::onGrabSelectionClicked()
{
    QClipboard *cb = QApplication::clipboard();
    // Try primary selection (Linux X11) first, then clipboard
    QString text = cb->text(QClipboard::Selection).trimmed();
    if (text.isEmpty())
        text = cb->text(QClipboard::Clipboard).trimmed();

    if (!text.isEmpty())
        setSourceText(text);
}

void TranslationDialog::onSwapLanguages()
{
    const QString srcCode = m_sourceLangCombo->currentData().toString();
    const QString tgtCode = m_targetLangCombo->currentData().toString();

    // Move translation result to source
    if (m_lastResult.success && !m_lastResult.translatedText.isEmpty())
        m_sourceEdit->setPlainText(m_lastResult.translatedText);

    // Swap combos (skip "auto" for source swap)
    int newSrcIdx = m_sourceLangCombo->findData(tgtCode);
    if (newSrcIdx < 0) newSrcIdx = 0;
    m_sourceLangCombo->setCurrentIndex(newSrcIdx);

    // Set target to old source (skip "auto")
    const QString newTgt = (srcCode == QLatin1String("auto") && !m_lastResult.detectedSourceLanguage.isEmpty())
                            ? m_lastResult.detectedSourceLanguage : srcCode;
    int newTgtIdx = m_targetLangCombo->findData(newTgt);
    if (newTgtIdx < 0) newTgtIdx = 0;
    m_targetLangCombo->setCurrentIndex(newTgtIdx);

    // Retranslate
    if (m_autoTranslate)
        onTranslateClicked();
}

void TranslationDialog::onCopyTranslation()
{
    if (m_lastResult.translatedText.isEmpty()) return;
    QApplication::clipboard()->setText(m_lastResult.translatedText);
    m_copyBtn->setText(QStringLiteral("✓ Copied!"));
    QTimer::singleShot(2000, this, [this]() {
        m_copyBtn->setText(QStringLiteral("📋 Copy"));
    });
}

void TranslationDialog::onSendToChat()
{
    if (m_lastResult.translatedText.isEmpty()) return;
    const QString formatted = QStringLiteral(
        "**Translation** (%1 → %2): %3")
        .arg(TranslationAssistant::languageDisplayName(m_lastRequest.sourceLanguage),
             TranslationAssistant::languageDisplayName(m_lastRequest.targetLanguage),
             m_lastResult.translatedText);
    emit sendToChatRequested(formatted);
    close();
}

void TranslationDialog::onToggleBreakdown()
{
    m_breakdownVisible = !m_breakdownVisible;
    m_breakdownBrowser->setVisible(m_breakdownVisible);
    m_toggleBreakdownBtn->setText(m_breakdownVisible
        ? QStringLiteral("▼  Pronunciation & Grammar Details")
        : QStringLiteral("▶  Pronunciation & Grammar Details"));
}

void TranslationDialog::onClearSource()
{
    m_sourceEdit->clear();
    m_resultBrowser->clear();
    m_breakdownBrowser->clear();
    m_statusLabel->clear();
    m_detectedLangLabel->clear();
    m_charCountLabel->setText(QStringLiteral("0 chars"));
    m_copyBtn->setEnabled(false);
    m_speakResultBtn->setEnabled(false);
    m_sendToChatBtn->setEnabled(false);
    m_favoriteBtn->setEnabled(false);
    m_lastResult = {};
}

void TranslationDialog::onSpeakSource()
{
    const QString text = m_sourceEdit->toPlainText().trimmed();
    if (text.isEmpty()) return;
    const QString lang = m_sourceLangCombo->currentData().toString();
    emit speakRequested(text, lang == QLatin1String("auto") ? QStringLiteral("en") : lang);
}

void TranslationDialog::onSpeakTranslation()
{
    if (m_lastResult.translatedText.isEmpty()) return;
    emit speakRequested(m_lastResult.translatedText, m_lastRequest.targetLanguage);
}

void TranslationDialog::onFavoriteClicked()
{
    if (m_lastResult.translatedText.isEmpty()) return;
    const QList<TranslationHistoryEntry> hist = m_assistant->history();
    if (!hist.isEmpty()) {
        m_assistant->setFavorite(hist.first().id, true);
        m_favoriteBtn->setText(QStringLiteral("⭐ Favorited"));
        m_favoriteBtn->setEnabled(false);
        refreshHistory();
    }
}

void TranslationDialog::onHistoryItemClicked(QListWidgetItem *item)
{
    if (!item) return;
    const QString src  = item->data(Qt::UserRole + 0).toString();
    const QString tgt  = item->data(Qt::UserRole + 1).toString();
    const QString text = item->data(Qt::UserRole + 2).toString();
    const QString tgtLang = item->data(Qt::UserRole + 3).toString();

    m_sourceEdit->setPlainText(text);

    const int tgtIdx = m_targetLangCombo->findData(tgtLang);
    if (tgtIdx >= 0) m_targetLangCombo->setCurrentIndex(tgtIdx);

    // Show cached result
    TranslationResult cached;
    cached.translatedText = tgt;
    cached.isCached = true;
    cached.success = true;
    TranslationRequest req;
    req.text = text;
    req.sourceLanguage = src;
    req.targetLanguage = tgtLang;
    displayResult(cached, req);
}

void TranslationDialog::onHistorySearchChanged(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        refreshHistory();
        return;
    }
    const QList<TranslationHistoryEntry> results = m_assistant->searchHistory(text);
    m_historyList->clear();
    for (const TranslationHistoryEntry &e : results) {
        const QString display = QStringLiteral("%1 → %2\n%3")
            .arg(TranslationAssistant::languageDisplayName(e.sourceLang),
                 TranslationAssistant::languageDisplayName(e.targetLang),
                 e.sourceText.left(60));
        auto *item = new QListWidgetItem(e.isFavorite ? QStringLiteral("⭐ ") + display : display,
                                          m_historyList);
        item->setData(Qt::UserRole + 0, e.sourceLang);
        item->setData(Qt::UserRole + 1, e.translatedText);
        item->setData(Qt::UserRole + 2, e.sourceText);
        item->setData(Qt::UserRole + 3, e.targetLang);
        item->setToolTip(QStringLiteral("→ %1").arg(e.translatedText.left(120)));
    }
}

void TranslationDialog::onClearHistory()
{
    m_assistant->clearHistory();
    m_historyList->clear();
}

// ─────────────────────────────────────────────────────────────
//  Translation Callbacks
// ─────────────────────────────────────────────────────────────
void TranslationDialog::onTranslationReady(const TranslationResult &result,
                                             const TranslationRequest &req)
{
    setTranslating(false);
    m_lastResult  = result;
    m_lastRequest = req;
    displayResult(result, req);
    refreshHistory();

    if (!result.detectedSourceLanguage.isEmpty()) {
        const LanguageInfo info = TranslationAssistant::languageByCode(result.detectedSourceLanguage);
        if (!info.name.isEmpty())
            m_detectedLangLabel->setText(QStringLiteral("%1 %2").arg(info.flag, info.name));
    }
}

void TranslationDialog::onTranslationError(const QString &error, const TranslationRequest &)
{
    setTranslating(false);
    m_statusLabel->setText(QStringLiteral("❌ %1").arg(error));
    m_resultBrowser->setHtml(QStringLiteral(
        "<p style='color:#ef4444;'>Translation failed: %1</p>").arg(error.toHtmlEscaped()));
}

void TranslationDialog::onTranslationProgress(const QString &status)
{
    m_statusLabel->setText(status);
}

// ─────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────
void TranslationDialog::setTranslating(bool busy)
{
    m_translateBtn->setEnabled(!busy);
    m_translateBtn->setText(busy ? QStringLiteral("⏳ Translating…")
                                 : QStringLiteral("⚡  Translate"));
    if (busy) {
        m_statusLabel->setText(QStringLiteral("Translating…"));
        m_copyBtn->setEnabled(false);
        m_speakResultBtn->setEnabled(false);
        m_sendToChatBtn->setEnabled(false);
        m_favoriteBtn->setEnabled(false);
    }
}

void TranslationDialog::displayResult(const TranslationResult &result,
                                       const TranslationRequest &req)
{
    if (!result.success) {
        m_resultBrowser->setHtml(QStringLiteral(
            "<p style='color:#ef4444;'>%1</p>").arg(result.errorMessage.toHtmlEscaped()));
        return;
    }

    // Main translation displayed as large styled text
    const QString srcLang = TranslationAssistant::languageDisplayName(req.sourceLanguage);
    const QString tgtLang = TranslationAssistant::languageDisplayName(req.targetLanguage);
    const QString cacheLabel = result.isCached ? QStringLiteral(" <span style='color:#f59e0b;font-size:11px;'>⚡ cached</span>") : QString();

    QString html = QStringLiteral(
        "<div style='font-family:sans-serif;'>"
        "<p style='color:#64748b;font-size:12px;margin:0 0 6px;'>%1 → %2%3</p>"
        "<p style='color:#f1f5f9;font-size:18px;font-weight:600;line-height:1.5;margin:0 0 4px;'>%4</p>"
    ).arg(srcLang.toHtmlEscaped(), tgtLang.toHtmlEscaped(), cacheLabel,
          result.translatedText.toHtmlEscaped());

    if (!result.pronunciation.isEmpty())
        html += QStringLiteral("<p style='color:#22d3ee;font-size:13px;font-style:italic;margin:2px 0;'>🔊 %1</p>")
                .arg(result.pronunciation.toHtmlEscaped());

    if (!result.alternatives.isEmpty())
        html += QStringLiteral("<p style='color:#94a3b8;font-size:12px;margin:2px 0;'>💡 <em>Alt:</em> %1</p>")
                .arg(result.alternatives.join(QStringLiteral(", ")).toHtmlEscaped());

    html += QStringLiteral("</div>");
    m_resultBrowser->setHtml(html);

    // Breakdown section
    if (!result.grammarNotes.isEmpty() || !result.exampleSentence.isEmpty()) {
        showBreakdownSection(result);
    } else {
        hideBreakdownSection();
    }

    m_statusLabel->setText(result.isCached ? QStringLiteral("⚡ From cache")
                           : result.isOfflineFallback ? QStringLiteral("📖 Offline phrasebook")
                           : QStringLiteral("✅ Translated"));

    m_copyBtn->setEnabled(true);
    m_speakResultBtn->setEnabled(true);
    m_sendToChatBtn->setEnabled(true);
    m_favoriteBtn->setEnabled(true);
    m_favoriteBtn->setText(QStringLiteral("☆ Favorite"));
}

void TranslationDialog::showBreakdownSection(const TranslationResult &result)
{
    QString html = QStringLiteral("<div style='font-family:sans-serif;font-size:13px;'>");
    if (!result.grammarNotes.isEmpty())
        html += QStringLiteral("<p><b style='color:#818cf8;'>📚 Grammar Notes:</b> %1</p>")
                .arg(result.grammarNotes.toHtmlEscaped());
    if (!result.exampleSentence.isEmpty())
        html += QStringLiteral("<p><b style='color:#818cf8;'>💬 Example:</b> <em>%1</em></p>")
                .arg(result.exampleSentence.toHtmlEscaped());
    html += QStringLiteral("</div>");
    m_breakdownBrowser->setHtml(html);
    m_toggleBreakdownBtn->setVisible(true);
}

void TranslationDialog::hideBreakdownSection()
{
    m_breakdownBrowser->clear();
    m_toggleBreakdownBtn->setVisible(false);
    m_breakdownBrowser->hide();
    m_breakdownVisible = false;
}

void TranslationDialog::refreshHistory()
{
    m_historyList->clear();
    const QList<TranslationHistoryEntry> entries = m_assistant->history();
    for (const TranslationHistoryEntry &e : entries) {
        const QString display = QStringLiteral("%1 → %2\n%3")
            .arg(TranslationAssistant::languageDisplayName(e.sourceLang),
                 TranslationAssistant::languageDisplayName(e.targetLang),
                 e.sourceText.left(60));
        auto *item = new QListWidgetItem(e.isFavorite ? QStringLiteral("⭐ ") + display : display,
                                          m_historyList);
        item->setData(Qt::UserRole + 0, e.sourceLang);
        item->setData(Qt::UserRole + 1, e.translatedText);
        item->setData(Qt::UserRole + 2, e.sourceText);
        item->setData(Qt::UserRole + 3, e.targetLang);
        item->setToolTip(QStringLiteral("→ %1").arg(e.translatedText.left(120)));
    }
}

#include "moc_translation_dialog.cpp"
