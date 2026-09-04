#ifndef TITANAI_CHAT_HISTORY_DIALOG_HPP
#define TITANAI_CHAT_HISTORY_DIALOG_HPP

#include <QDialog>
#include <QList>
#include "agent/chat_history_manager.hpp"

class QLineEdit;
class QPushButton;
class QListWidget;
class QListWidgetItem;
class QTextBrowser;
class QLabel;
class QSplitter;
class QComboBox;
class QStackedWidget;

/**
 * @brief Full-screen modal panel for browsing and searching all chat conversations.
 *
 * Layout:
 *  ┌──────────────────────────────────────────────────────────────────┐
 *  │  🔍 [search bar]  [role filter ▾]           [New Chat] [Close]  │
 *  ├──────────────────┬───────────────────────────────────────────────┤
 *  │  Session List    │  Preview / Search Results                     │
 *  │  ─────────────   │                                               │
 *  │  Conv 1          │  (formatted transcript or result cards)       │
 *  │  Conv 2          │                                               │
 *  │  …               │                                               │
 *  │                  │          [Load Conversation]                  │
 *  └──────────────────┴───────────────────────────────────────────────┘
 */
class ChatHistoryDialog : public QDialog {
    Q_OBJECT

public:
    explicit ChatHistoryDialog(ChatHistoryManager *historyManager, QWidget *parent = nullptr);
    ~ChatHistoryDialog() override = default;

signals:
    /// Emitted when the user wants to load a session into the main chat window.
    void loadSessionRequested(const QString &sessionId);
    /// Emitted when the user creates a new conversation from this dialog.
    void newConversationRequested();

private slots:
    void onSearchTextChanged(const QString &text);
    void onSessionSelected(QListWidgetItem *item);
    void onLoadConversation();
    void onNewChat();
    void onDeleteSession();
    void onRenameSession();
    void onExportMarkdown();
    void onExportText();
    void onRoleFilterChanged(int index);
    void refreshSessionList();

private:
    void setupUi();
    void setupStylesheet();
    void populateSessionList();
    void showSessionPreview(const QString &sessionId);
    void showSearchResults(const QList<SearchResult> &results, const QString &query);
    void clearPreview();

    ChatHistoryManager   *m_history{nullptr};
    QString               m_selectedSessionId;
    bool                  m_inSearchMode{false};

    // Widgets
    QLineEdit         *m_searchEdit{nullptr};
    QComboBox         *m_roleFilter{nullptr};
    QPushButton       *m_newChatBtn{nullptr};
    QPushButton       *m_closeBtn{nullptr};
    QListWidget       *m_sessionList{nullptr};
    QPushButton       *m_deleteBtn{nullptr};
    QPushButton       *m_renameBtn{nullptr};
    QPushButton       *m_exportMdBtn{nullptr};
    QPushButton       *m_exportTxtBtn{nullptr};
    QTextBrowser      *m_previewBrowser{nullptr};
    QPushButton       *m_loadBtn{nullptr};
    QLabel            *m_resultCountLabel{nullptr};
    QLabel            *m_emptyLabel{nullptr};
    QSplitter         *m_splitter{nullptr};
};

#endif // TITANAI_CHAT_HISTORY_DIALOG_HPP
