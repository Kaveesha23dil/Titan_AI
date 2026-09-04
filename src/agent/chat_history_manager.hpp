#ifndef TITANAI_CHAT_HISTORY_MANAGER_HPP
#define TITANAI_CHAT_HISTORY_MANAGER_HPP

#include <QObject>
#include <QString>
#include <QList>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>

// ─────────────────────────────────────────────────────────────────────────────
//  Data structures
// ─────────────────────────────────────────────────────────────────────────────

/// A single message within a conversation session.
struct ChatMessage {
    QString  id;           ///< Unique message identifier (UUID)
    QString  role;         ///< "user" | "assistant" | "system" | "tool" | "error"
    QString  content;      ///< Full text content
    QDateTime timestamp;   ///< Wall-clock time when the message was recorded
    bool     hasImage{false}; ///< True when an image was attached

    [[nodiscard]] QJsonObject toJson() const;
    static ChatMessage fromJson(const QJsonObject &obj);
};

/// Metadata summary for a session (used in list views without loading all messages).
struct ChatSessionSummary {
    QString   id;
    QString   title;
    QDateTime createdAt;
    QDateTime updatedAt;
    int       messageCount{0};
    QString   lastSnippet;   ///< Truncated first 80 chars of the last message
};

/// A full conversation session with all its messages.
struct ChatSession {
    QString   id;
    QString   title;
    QDateTime createdAt;
    QDateTime updatedAt;
    QList<ChatMessage> messages;

    [[nodiscard]] ChatSessionSummary toSummary() const;
    [[nodiscard]] QJsonObject toJson() const;
    static ChatSession fromJson(const QJsonObject &obj);
};

/// One search result referencing a matched message in a session.
struct SearchResult {
    QString   sessionId;
    QString   sessionTitle;
    QString   messageId;
    QString   role;
    QDateTime timestamp;
    QString   matchedSnippet;  ///< Surrounding text with the match highlighted (HTML)
    int       matchCount{0};
    double    score{0.0};      ///< Relevance score (higher = better)
};

/// Filtering options for search queries.
struct SearchFilter {
    QString  roleFilter;           ///< Empty = any; "user" | "assistant" | ...
    QDateTime fromDate;            ///< Invalid = no lower date bound
    QDateTime toDate;              ///< Invalid = no upper date bound
    QString  sessionId;            ///< Empty = search all sessions
    int      maxResults{50};       ///< Maximum number of results returned
    bool     caseSensitive{false};
};

// ─────────────────────────────────────────────────────────────────────────────
//  ChatHistoryManager
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Manages persistent multi-session chat history and full-text search.
 *
 * Each conversation is stored as a JSON file under the platform-appropriate
 * user-data directory (~/.local/share/TitanAI/conversations/).  An in-memory
 * index of session summaries allows fast listing and searching without loading
 * full transcripts until needed.
 */
class ChatHistoryManager : public QObject {
    Q_OBJECT

public:
    explicit ChatHistoryManager(QObject *parent = nullptr);
    ~ChatHistoryManager() override = default;

    // ── Session lifecycle ────────────────────────────────────────────────────

    /// Creates a new session (optionally with a custom title) and makes it active.
    /// Returns the new session's ID.
    QString createSession(const QString &title = QString());

    /// Returns the ID of the currently active session (empty if none).
    [[nodiscard]] QString currentSessionId() const;

    /// Returns a copy of the currently active session (or a default-constructed
    /// ChatSession if none is active).
    [[nodiscard]] ChatSession currentSession() const;

    /// Loads and activates an existing session by ID.
    /// Returns false if the session does not exist.
    bool switchSession(const QString &sessionId);

    /// Permanently deletes a session and its storage file.
    bool deleteSession(const QString &sessionId);

    /// Renames a session.
    bool renameSession(const QString &sessionId, const QString &newTitle);

    /// Returns metadata summaries for all sessions, newest first.
    [[nodiscard]] QList<ChatSessionSummary> allSessions() const;

    /// Returns the total number of sessions.
    [[nodiscard]] int sessionCount() const;

    /// Deletes all sessions and their storage files.
    void clearAll();

    // ── Message logging ──────────────────────────────────────────────────────

    /// Appends a message to the currently active session and persists it.
    void appendMessage(const QString &role, const QString &text, bool hasImage = false);

    // ── Full-text search ─────────────────────────────────────────────────────

    /**
     * @brief Full-text search across all (or filtered) sessions.
     *
     * Supports:
     *  - Single and multi-word queries (AND semantics – all words must match).
     *  - Case-insensitive matching by default.
     *  - Role, date, and per-session filtering via @p filter.
     *  - Snippet extraction with up to 60 chars of context on each side.
     *  - Results sorted by relevance score (descending).
     */
    [[nodiscard]] QList<SearchResult> search(const QString &query,
                                              const SearchFilter &filter = {}) const;

    /// Loads and returns the full session by ID (from disk). Returns a default-constructed
    /// ChatSession (with empty id) if the session does not exist.
    [[nodiscard]] ChatSession loadSession(const QString &sessionId) const;

    // ── Export ───────────────────────────────────────────────────────────────

    /// Exports a session as a Markdown transcript string.
    [[nodiscard]] QString exportToMarkdown(const QString &sessionId) const;

    /// Exports a session as a plain-text transcript string.
    [[nodiscard]] QString exportToPlainText(const QString &sessionId) const;

    // ── Persistence helpers ──────────────────────────────────────────────────

    /// Returns the directory where sessions are stored on disk.
    [[nodiscard]] QString storageDirectory() const;

signals:
    /// Emitted after a new session has been created.
    void sessionCreated(const QString &sessionId);
    /// Emitted after the active session changes.
    void sessionSwitched(const QString &sessionId);
    /// Emitted whenever a message has been appended to the active session.
    void messageAppended(const ChatMessage &message);
    /// Emitted after a session is deleted.
    void sessionDeleted(const QString &sessionId);
    /// Emitted when the session list has changed (create / delete / rename).
    void sessionsChanged();

private:
    // Internal helpers
    [[nodiscard]] QString sessionFilePath(const QString &sessionId) const;
    bool saveSession(const ChatSession &session) const;
    [[nodiscard]] ChatSession loadSessionFromDisk(const QString &sessionId) const;
    void rebuildIndex();
    void updateIndexEntry(const ChatSession &session);
    void removeIndexEntry(const QString &sessionId);

    static QString generateId();
    static QString generateTitle(const QString &firstMessage);
    static QString buildSnippet(const QString &content, const QStringList &terms,
                                 bool caseSensitive, int contextChars = 60);
    static QString highlightTerms(const QString &snippet, const QStringList &terms,
                                   bool caseSensitive);

    QString              m_storageDir;
    QString              m_currentSessionId;
    ChatSession          m_currentSession;   ///< In-memory active session
    QList<ChatSessionSummary> m_index;       ///< Lightweight metadata index
};

#endif // TITANAI_CHAT_HISTORY_MANAGER_HPP
