#include "gui/chat_history_dialog.hpp"

#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>

// ─────────────────────────────────────────────────────────────────────────────
//  Helper: relative time string
// ─────────────────────────────────────────────────────────────────────────────
static QString relativeTime(const QDateTime &dt)
{
    const qint64 secs = dt.secsTo(QDateTime::currentDateTime());
    if (secs < 60)  return QStringLiteral("just now");
    if (secs < 3600) return QStringLiteral("%1m ago").arg(secs / 60);
    if (secs < 86400) return QStringLiteral("%1h ago").arg(secs / 3600);
    if (secs < 86400 * 7) return QStringLiteral("%1d ago").arg(secs / 86400);
    return dt.toString(QStringLiteral("MMM d, yyyy"));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Construction
// ─────────────────────────────────────────────────────────────────────────────
ChatHistoryDialog::ChatHistoryDialog(ChatHistoryManager *historyManager, QWidget *parent)
    : QDialog(parent)
    , m_history(historyManager)
{
    setWindowTitle(QStringLiteral("Chat History"));
    setMinimumSize(860, 580);
    resize(1060, 680);
    setModal(true);

    setupUi();
    setupStylesheet();
    populateSessionList();

    // Esc closes
    QShortcut *esc = new QShortcut(QKeySequence::Cancel, this);
    connect(esc, &QShortcut::activated, this, &QDialog::reject);
}

// ─────────────────────────────────────────────────────────────────────────────
//  UI Setup
// ─────────────────────────────────────────────────────────────────────────────
void ChatHistoryDialog::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Header bar ──────────────────────────────────────────────────────────
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("historyHeader"));
    header->setFixedHeight(62);
    auto *hLayout = new QHBoxLayout(header);
    hLayout->setContentsMargins(20, 0, 20, 0);
    hLayout->setSpacing(10);

    // Search icon label
    auto *searchIcon = new QLabel(QStringLiteral("🔍"), header);
    searchIcon->setObjectName(QStringLiteral("searchIcon"));
    hLayout->addWidget(searchIcon);

    m_searchEdit = new QLineEdit(header);
    m_searchEdit->setObjectName(QStringLiteral("historySearch"));
    m_searchEdit->setPlaceholderText(QStringLiteral("Search all conversations…"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setMinimumWidth(280);
    hLayout->addWidget(m_searchEdit, 1);

    m_roleFilter = new QComboBox(header);
    m_roleFilter->setObjectName(QStringLiteral("roleFilter"));
    m_roleFilter->addItem(QStringLiteral("All roles"),      QStringLiteral(""));
    m_roleFilter->addItem(QStringLiteral("My messages"),    QStringLiteral("user"));
    m_roleFilter->addItem(QStringLiteral("AI responses"),   QStringLiteral("assistant"));
    m_roleFilter->addItem(QStringLiteral("Tool output"),    QStringLiteral("tool"));
    hLayout->addWidget(m_roleFilter);

    m_resultCountLabel = new QLabel(header);
    m_resultCountLabel->setObjectName(QStringLiteral("resultCount"));
    m_resultCountLabel->hide();
    hLayout->addWidget(m_resultCountLabel);

    hLayout->addStretch(1);

    m_newChatBtn = new QPushButton(QStringLiteral("✦  New Chat"), header);
    m_newChatBtn->setObjectName(QStringLiteral("newChatBtn"));
    hLayout->addWidget(m_newChatBtn);

    m_closeBtn = new QPushButton(QStringLiteral("✕"), header);
    m_closeBtn->setObjectName(QStringLiteral("closeBtn"));
    m_closeBtn->setFixedSize(36, 36);
    hLayout->addWidget(m_closeBtn);

    root->addWidget(header);

    // ── Separator ────────────────────────────────────────────────────────────
    auto *sep = new QWidget(this);
    sep->setFixedHeight(1);
    sep->setObjectName(QStringLiteral("separator"));
    root->addWidget(sep);

    // ── Split area ───────────────────────────────────────────────────────────
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(1);
    m_splitter->setObjectName(QStringLiteral("mainSplitter"));

    // ── Left pane: session list ───────────────────────────────────────────────
    auto *leftPane = new QWidget(m_splitter);
    leftPane->setObjectName(QStringLiteral("leftPane"));
    auto *leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    // Session list header
    auto *listHeader = new QWidget(leftPane);
    listHeader->setObjectName(QStringLiteral("listHeader"));
    listHeader->setFixedHeight(38);
    auto *listHeaderLayout = new QHBoxLayout(listHeader);
    listHeaderLayout->setContentsMargins(16, 0, 8, 0);
    auto *listTitle = new QLabel(QStringLiteral("Conversations"), listHeader);
    listTitle->setObjectName(QStringLiteral("listTitle"));
    listHeaderLayout->addWidget(listTitle);
    listHeaderLayout->addStretch(1);
    leftLayout->addWidget(listHeader);

    m_sessionList = new QListWidget(leftPane);
    m_sessionList->setObjectName(QStringLiteral("sessionList"));
    m_sessionList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sessionList->setSelectionMode(QAbstractItemView::SingleSelection);
    leftLayout->addWidget(m_sessionList, 1);

    // Action buttons strip
    auto *actionBar = new QWidget(leftPane);
    actionBar->setObjectName(QStringLiteral("actionBar"));
    actionBar->setFixedHeight(46);
    auto *actionLayout = new QHBoxLayout(actionBar);
    actionLayout->setContentsMargins(10, 6, 10, 6);
    actionLayout->setSpacing(6);

    m_renameBtn = new QPushButton(QStringLiteral("✏ Rename"), actionBar);
    m_renameBtn->setObjectName(QStringLiteral("renameBtn"));
    m_deleteBtn = new QPushButton(QStringLiteral("🗑 Delete"), actionBar);
    m_deleteBtn->setObjectName(QStringLiteral("deleteBtn"));

    m_exportMdBtn  = new QPushButton(QStringLiteral("↓ Markdown"), actionBar);
    m_exportMdBtn->setObjectName(QStringLiteral("exportMdBtn"));
    m_exportTxtBtn = new QPushButton(QStringLiteral("↓ Text"), actionBar);
    m_exportTxtBtn->setObjectName(QStringLiteral("exportTxtBtn"));

    actionLayout->addWidget(m_renameBtn);
    actionLayout->addWidget(m_deleteBtn);
    actionLayout->addStretch(1);
    actionLayout->addWidget(m_exportMdBtn);
    actionLayout->addWidget(m_exportTxtBtn);
    leftLayout->addWidget(actionBar);

    m_splitter->addWidget(leftPane);

    // ── Right pane: preview ───────────────────────────────────────────────────
    auto *rightPane = new QWidget(m_splitter);
    rightPane->setObjectName(QStringLiteral("rightPane"));
    auto *rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    m_previewBrowser = new QTextBrowser(rightPane);
    m_previewBrowser->setObjectName(QStringLiteral("previewBrowser"));
    m_previewBrowser->setReadOnly(true);
    m_previewBrowser->setOpenExternalLinks(false);
    m_previewBrowser->setFrameShape(QFrame::NoFrame);
    m_previewBrowser->document()->setDefaultStyleSheet(QStringLiteral(
        "body { margin: 0; padding: 0; background: #0b0f19; color: #f1f5f9; }"
        ".msg-user { background: #1a1540; border-left: 3px solid #22d3ee; margin: 4px 16px; padding: 10px 14px; border-radius: 6px; }"
        ".msg-assistant { background: #151d2e; border-left: 3px solid #6366f1; margin: 4px 16px; padding: 10px 14px; border-radius: 6px; }"
        ".msg-error { background: #2a1215; border-left: 3px solid #ef4444; margin: 4px 16px; padding: 10px 14px; border-radius: 6px; }"
        ".msg-tool { background: #111520; border-left: 3px solid #64748b; margin: 4px 16px; padding: 10px 14px; border-radius: 6px; }"
        ".sender { font-size: 11px; font-weight: 700; margin-bottom: 4px; }"
        ".ts { font-size: 10px; color: #64748b; }"
        ".content { font-size: 13px; color: #f1f5f9; }"
        "mark { background: #f59e0b; color: #0f172a; border-radius: 2px; padding: 0 2px; }"
        ".search-result-card { background: #151d2e; border: 1px solid #1e293b; border-radius: 8px; margin: 8px 16px; padding: 12px 16px; }"
        ".result-header { font-size: 11px; color: #94a3b8; margin-bottom: 6px; }"
        ".result-snippet { font-size: 13px; color: #f1f5f9; }"
    ));
    rightLayout->addWidget(m_previewBrowser, 1);

    // Empty state label
    m_emptyLabel = new QLabel(QStringLiteral(
        "💬\n\nSelect a conversation to preview\nor start searching above."), rightPane);
    m_emptyLabel->setObjectName(QStringLiteral("emptyLabel"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    // Overlaid via stacked approach – place in right pane but manage visibility
    m_emptyLabel->setParent(m_previewBrowser);
    m_emptyLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    // Load button
    auto *loadBar = new QWidget(rightPane);
    loadBar->setObjectName(QStringLiteral("loadBar"));
    loadBar->setFixedHeight(56);
    auto *loadLayout = new QHBoxLayout(loadBar);
    loadLayout->setContentsMargins(16, 8, 16, 8);
    loadLayout->addStretch(1);
    m_loadBtn = new QPushButton(QStringLiteral("📂  Load Conversation into Chat"), loadBar);
    m_loadBtn->setObjectName(QStringLiteral("loadBtn"));
    m_loadBtn->setEnabled(false);
    loadLayout->addWidget(m_loadBtn);
    loadLayout->addStretch(1);
    rightLayout->addWidget(loadBar);

    m_splitter->addWidget(rightPane);
    m_splitter->setSizes({300, 760});

    root->addWidget(m_splitter, 1);

    // ── Connections ───────────────────────────────────────────────────────────
    connect(m_searchEdit,  &QLineEdit::textChanged,     this, &ChatHistoryDialog::onSearchTextChanged);
    connect(m_roleFilter,  QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ChatHistoryDialog::onRoleFilterChanged);
    connect(m_sessionList, &QListWidget::itemClicked,   this, &ChatHistoryDialog::onSessionSelected);
    connect(m_loadBtn,     &QPushButton::clicked,       this, &ChatHistoryDialog::onLoadConversation);
    connect(m_newChatBtn,  &QPushButton::clicked,       this, &ChatHistoryDialog::onNewChat);
    connect(m_deleteBtn,   &QPushButton::clicked,       this, &ChatHistoryDialog::onDeleteSession);
    connect(m_renameBtn,   &QPushButton::clicked,       this, &ChatHistoryDialog::onRenameSession);
    connect(m_exportMdBtn, &QPushButton::clicked,       this, &ChatHistoryDialog::onExportMarkdown);
    connect(m_exportTxtBtn,&QPushButton::clicked,       this, &ChatHistoryDialog::onExportText);
    connect(m_closeBtn,    &QPushButton::clicked,       this, &QDialog::reject);

    // Refresh list when history changes
    if (m_history) {
        connect(m_history, &ChatHistoryManager::sessionsChanged,
                this, &ChatHistoryDialog::refreshSessionList);
    }
}

void ChatHistoryDialog::setupStylesheet()
{
    setStyleSheet(QStringLiteral(R"(
        ChatHistoryDialog {
            background: #0b0f19;
        }
        #historyHeader {
            background: #111827;
            border-bottom: 1px solid #1e293b;
        }
        #separator {
            background: #1e293b;
        }
        #leftPane {
            background: #070a13;
        }
        #listHeader {
            background: #0a0e1a;
            border-bottom: 1px solid #1e293b;
        }
        #listTitle {
            color: #94a3b8;
            font-size: 11px;
            font-weight: 600;
            letter-spacing: 1px;
            text-transform: uppercase;
        }
        #sessionList {
            background: #070a13;
            border: none;
            outline: none;
            color: #f1f5f9;
            font-size: 13px;
        }
        #sessionList::item {
            padding: 12px 14px;
            border-bottom: 1px solid #111827;
        }
        #sessionList::item:selected {
            background: #1a2236;
            border-left: 3px solid #6366f1;
            color: #f1f5f9;
        }
        #sessionList::item:hover:!selected {
            background: #0f1629;
        }
        #actionBar {
            background: #0a0e1a;
            border-top: 1px solid #1e293b;
        }
        #renameBtn, #deleteBtn, #exportMdBtn, #exportTxtBtn {
            background: #111827;
            color: #94a3b8;
            border: 1px solid #1e293b;
            border-radius: 6px;
            padding: 4px 10px;
            font-size: 11px;
        }
        #renameBtn:hover, #exportMdBtn:hover, #exportTxtBtn:hover {
            background: #1a2236;
            color: #f1f5f9;
            border-color: #6366f1;
        }
        #deleteBtn:hover {
            background: #2a1215;
            color: #ef4444;
            border-color: #ef4444;
        }
        #rightPane {
            background: #0b0f19;
        }
        #previewBrowser {
            background: #0b0f19;
            border: none;
            color: #f1f5f9;
            padding: 0;
        }
        #loadBar {
            background: #111827;
            border-top: 1px solid #1e293b;
        }
        #loadBtn {
            background: #6366f1;
            color: white;
            border: none;
            border-radius: 8px;
            padding: 10px 24px;
            font-size: 13px;
            font-weight: 600;
        }
        #loadBtn:hover {
            background: #818cf8;
        }
        #loadBtn:disabled {
            background: #1e293b;
            color: #64748b;
        }
        #newChatBtn {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #6366f1,stop:1 #22d3ee);
            color: white;
            border: none;
            border-radius: 8px;
            padding: 8px 20px;
            font-size: 13px;
            font-weight: 600;
        }
        #newChatBtn:hover {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #818cf8,stop:1 #38bdf8);
        }
        #closeBtn {
            background: transparent;
            color: #64748b;
            border: 1px solid #1e293b;
            border-radius: 6px;
            font-size: 14px;
        }
        #closeBtn:hover {
            background: #2a1215;
            color: #ef4444;
            border-color: #ef4444;
        }
        #historySearch {
            background: #0f1629;
            color: #f1f5f9;
            border: 1px solid #1e293b;
            border-radius: 8px;
            padding: 8px 14px;
            font-size: 13px;
        }
        #historySearch:focus {
            border-color: #6366f1;
        }
        #roleFilter {
            background: #0f1629;
            color: #94a3b8;
            border: 1px solid #1e293b;
            border-radius: 8px;
            padding: 6px 12px;
            font-size: 12px;
        }
        #resultCount {
            color: #6366f1;
            font-size: 12px;
            font-weight: 600;
        }
        #emptyLabel {
            color: #64748b;
            font-size: 15px;
        }
        QScrollBar:vertical {
            background: #0b0f19;
            width: 6px;
            border: none;
        }
        QScrollBar::handle:vertical {
            background: #1e293b;
            border-radius: 3px;
        }
        QScrollBar::handle:vertical:hover {
            background: #6366f1;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QSplitter::handle {
            background: #1e293b;
        }
    )"));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Session List Population
// ─────────────────────────────────────────────────────────────────────────────
void ChatHistoryDialog::populateSessionList()
{
    if (!m_history) { return; }

    m_sessionList->clear();

    const QList<ChatSessionSummary> sessions = m_history->allSessions();
    for (const ChatSessionSummary &s : sessions) {
        auto *item = new QListWidgetItem(m_sessionList);
        item->setData(Qt::UserRole, s.id);

        // Build a two-line label widget for rich display
        const QString html = QStringLiteral(
            "<div style='color:#f1f5f9;font-size:13px;font-weight:600;'>%1</div>"
            "<div style='color:#64748b;font-size:11px;margin-top:2px;'>%2 messages · %3</div>"
        ).arg(s.title.toHtmlEscaped(),
              QString::number(s.messageCount),
              relativeTime(s.updatedAt));

        auto *label = new QLabel(html);
        label->setContentsMargins(4, 0, 4, 0);
        label->setTextFormat(Qt::RichText);
        label->setWordWrap(false);
        item->setSizeHint(QSize(0, 58));
        m_sessionList->addItem(item);
        m_sessionList->setItemWidget(item, label);

        // Auto-select the current active session
        if (s.id == m_history->currentSessionId()) {
            m_sessionList->setCurrentItem(item);
            m_selectedSessionId = s.id;
        }
    }

    if (!m_selectedSessionId.isEmpty()) {
        showSessionPreview(m_selectedSessionId);
    } else if (!sessions.isEmpty()) {
        m_selectedSessionId = sessions.first().id;
        m_sessionList->setCurrentRow(0);
        showSessionPreview(m_selectedSessionId);
    }
}

void ChatHistoryDialog::refreshSessionList()
{
    populateSessionList();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Session Preview
// ─────────────────────────────────────────────────────────────────────────────
void ChatHistoryDialog::showSessionPreview(const QString &sessionId)
{
    if (!m_history) { return; }

    m_inSearchMode = false;
    m_emptyLabel->hide();

    const ChatSession session = m_history->loadSession(sessionId);

    // Build HTML preview of all messages
    QString html = QStringLiteral("<html><body style='background:#0b0f19;margin:0;padding:8px 0;'>");
    html += QStringLiteral(
        "<div style='padding:12px 16px 6px 16px;'>"
        "<span style='color:#6366f1;font-size:16px;font-weight:700;'>%1</span>"
        "<br><span style='color:#64748b;font-size:11px;'>%2 messages · Updated %3</span>"
        "</div>").arg(
            session.title.toHtmlEscaped(),
            QString::number(session.messages.size()),
            session.updatedAt.toString(QStringLiteral("yyyy-MM-dd hh:mm")));

    for (const ChatMessage &msg : session.messages) {
        const QString cssClass =
            msg.role == QStringLiteral("user")      ? QStringLiteral("msg-user") :
            msg.role == QStringLiteral("assistant") ? QStringLiteral("msg-assistant") :
            msg.role == QStringLiteral("error")     ? QStringLiteral("msg-error") :
            QStringLiteral("msg-tool");

        const QString roleLabel =
            msg.role == QStringLiteral("user")      ? QStringLiteral("👤 You") :
            msg.role == QStringLiteral("assistant") ? QStringLiteral("✦ TitanAI") :
            msg.role == QStringLiteral("error")     ? QStringLiteral("⚠ Error") :
            QStringLiteral("⚙ Tool");

        QString escaped = msg.content.toHtmlEscaped();
        escaped.replace(QLatin1Char('\n'), QStringLiteral("<br/>"));

        html += QStringLiteral(
            "<div class='%1'>"
            "<div class='sender'>%2 <span class='ts'>%3</span></div>"
            "<div class='content'>%4</div>"
            "%5"
            "</div>"
        ).arg(cssClass, roleLabel,
              msg.timestamp.toString(QStringLiteral("hh:mm")),
              escaped,
              msg.hasImage ? QStringLiteral("<div style='color:#6366f1;font-size:11px;margin-top:4px;'>📎 Image attached</div>") : QString());
    }

    if (session.messages.isEmpty()) {
        html += QStringLiteral("<div style='color:#64748b;text-align:center;padding:40px;font-size:14px;'>No messages in this conversation.</div>");
    }

    html += QStringLiteral("</body></html>");

    m_previewBrowser->setHtml(html);
    m_loadBtn->setEnabled(true);
}

void ChatHistoryDialog::showSearchResults(const QList<SearchResult> &results, const QString &query)
{
    m_inSearchMode = true;
    m_emptyLabel->hide();

    if (results.isEmpty()) {
        m_previewBrowser->setHtml(QStringLiteral(
            "<html><body style='background:#0b0f19;padding:40px;text-align:center;'>"
            "<span style='color:#64748b;font-size:16px;'>🔍 No results found for <b style='color:#f1f5f9;'>%1</b></span>"
            "</body></html>").arg(query.toHtmlEscaped()));
        m_loadBtn->setEnabled(false);
        return;
    }

    QString html = QStringLiteral(
        "<html><body style='background:#0b0f19;margin:0;padding:8px 0;'>"
        "<div style='padding:12px 16px 6px 16px;'>"
        "<span style='color:#6366f1;font-size:15px;font-weight:700;'>🔍 Results for \"%1\"</span>"
        "<br><span style='color:#64748b;font-size:11px;'>%2 match(es)</span>"
        "</div>").arg(query.toHtmlEscaped(), QString::number(results.size()));

    for (const SearchResult &r : results) {
        const QString roleLabel =
            r.role == QStringLiteral("user")      ? QStringLiteral("👤 You") :
            r.role == QStringLiteral("assistant") ? QStringLiteral("✦ TitanAI") :
            QStringLiteral("⚙ Tool");

        html += QStringLiteral(
            "<div class='search-result-card'>"
            "<div class='result-header'>%1 &nbsp;·&nbsp; in <b style='color:#818cf8;'>%2</b> &nbsp;·&nbsp; %3</div>"
            "<div class='result-snippet'>%4</div>"
            "</div>"
        ).arg(roleLabel, r.sessionTitle.toHtmlEscaped(),
              relativeTime(r.timestamp), r.matchedSnippet);
    }

    html += QStringLiteral("</body></html>");
    m_previewBrowser->setHtml(html);
    m_loadBtn->setEnabled(!results.isEmpty());
}

void ChatHistoryDialog::clearPreview()
{
    m_previewBrowser->clear();
    m_emptyLabel->show();
    m_emptyLabel->setGeometry(m_previewBrowser->rect());
    m_loadBtn->setEnabled(false);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Slots
// ─────────────────────────────────────────────────────────────────────────────
void ChatHistoryDialog::onSearchTextChanged(const QString &text)
{
    if (!m_history) { return; }

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        m_resultCountLabel->hide();
        m_inSearchMode = false;
        if (!m_selectedSessionId.isEmpty()) {
            showSessionPreview(m_selectedSessionId);
        } else {
            clearPreview();
        }
        return;
    }

    // Live search with slight debounce
    SearchFilter filter;
    filter.roleFilter = m_roleFilter->currentData().toString();
    filter.maxResults = 100;

    const QList<SearchResult> results = m_history->search(trimmed, filter);

    m_resultCountLabel->setText(QStringLiteral("%1 result(s)").arg(results.size()));
    m_resultCountLabel->show();

    showSearchResults(results, trimmed);

    // Highlight matching sessions in the list
    for (int i = 0; i < m_sessionList->count(); ++i) {
        QListWidgetItem *item = m_sessionList->item(i);
        const QString sessionId = item->data(Qt::UserRole).toString();
        bool hasMatch = false;
        for (const SearchResult &r : results) {
            if (r.sessionId == sessionId) { hasMatch = true; break; }
        }
        item->setForeground(hasMatch ? QColor(QStringLiteral("#818cf8"))
                                     : QColor(QStringLiteral("#64748b")));
    }

    // Select first matching session
    if (!results.isEmpty()) {
        m_selectedSessionId = results.first().sessionId;
    }
}

void ChatHistoryDialog::onSessionSelected(QListWidgetItem *item)
{
    if (!item) { return; }

    m_selectedSessionId = item->data(Qt::UserRole).toString();

    if (!m_searchEdit->text().trimmed().isEmpty()) {
        // In search mode – show filtered results for this session
        SearchFilter filter;
        filter.sessionId  = m_selectedSessionId;
        filter.roleFilter = m_roleFilter->currentData().toString();
        filter.maxResults = 100;
        const QList<SearchResult> results = m_history->search(m_searchEdit->text().trimmed(), filter);
        showSearchResults(results, m_searchEdit->text().trimmed());
    } else {
        showSessionPreview(m_selectedSessionId);
    }
}

void ChatHistoryDialog::onRoleFilterChanged(int /*index*/)
{
    onSearchTextChanged(m_searchEdit->text());
}

void ChatHistoryDialog::onLoadConversation()
{
    if (m_selectedSessionId.isEmpty()) { return; }
    emit loadSessionRequested(m_selectedSessionId);
    accept();
}

void ChatHistoryDialog::onNewChat()
{
    emit newConversationRequested();
    accept();
}

void ChatHistoryDialog::onDeleteSession()
{
    if (m_selectedSessionId.isEmpty() || !m_history) { return; }

    // Find the title for a meaningful confirmation message
    QString title;
    for (const ChatSessionSummary &s : m_history->allSessions()) {
        if (s.id == m_selectedSessionId) { title = s.title; break; }
    }

    const auto answer = QMessageBox::question(
        this,
        QStringLiteral("Delete Conversation"),
        QStringLiteral("Delete conversation \"%1\"?\nThis cannot be undone.").arg(title),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (answer != QMessageBox::Yes) { return; }

    m_history->deleteSession(m_selectedSessionId);
    m_selectedSessionId.clear();
    clearPreview();
}

void ChatHistoryDialog::onRenameSession()
{
    if (m_selectedSessionId.isEmpty() || !m_history) { return; }

    QString currentTitle;
    for (const ChatSessionSummary &s : m_history->allSessions()) {
        if (s.id == m_selectedSessionId) { currentTitle = s.title; break; }
    }

    bool ok = false;
    const QString newTitle = QInputDialog::getText(
        this,
        QStringLiteral("Rename Conversation"),
        QStringLiteral("New name:"),
        QLineEdit::Normal,
        currentTitle,
        &ok);

    if (ok && !newTitle.trimmed().isEmpty()) {
        m_history->renameSession(m_selectedSessionId, newTitle.trimmed());
    }
}

void ChatHistoryDialog::onExportMarkdown()
{
    if (m_selectedSessionId.isEmpty() || !m_history) { return; }

    const QString md = m_history->exportToMarkdown(m_selectedSessionId);
    if (md.isEmpty()) { return; }

    QString suggestedName;
    for (const ChatSessionSummary &s : m_history->allSessions()) {
        if (s.id == m_selectedSessionId) {
            suggestedName = s.title.simplified().replace(QLatin1Char(' '), QLatin1Char('_')) + QStringLiteral(".md");
            break;
        }
    }

    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export as Markdown"),
        suggestedName,
        QStringLiteral("Markdown Files (*.md)"));

    if (path.isEmpty()) { return; }

    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&f);
        out << md;
    }
}

void ChatHistoryDialog::onExportText()
{
    if (m_selectedSessionId.isEmpty() || !m_history) { return; }

    const QString txt = m_history->exportToPlainText(m_selectedSessionId);
    if (txt.isEmpty()) { return; }

    QString suggestedName;
    for (const ChatSessionSummary &s : m_history->allSessions()) {
        if (s.id == m_selectedSessionId) {
            suggestedName = s.title.simplified().replace(QLatin1Char(' '), QLatin1Char('_')) + QStringLiteral(".txt");
            break;
        }
    }

    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export as Plain Text"),
        suggestedName,
        QStringLiteral("Text Files (*.txt)"));

    if (path.isEmpty()) { return; }

    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&f);
        out << txt;
    }
}
