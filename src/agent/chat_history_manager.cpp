#include "agent/chat_history_manager.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QUuid>

// ─────────────────────────────────────────────────────────────────────────────
//  ChatMessage
// ─────────────────────────────────────────────────────────────────────────────

QJsonObject ChatMessage::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("id")]        = id;
    obj[QStringLiteral("role")]      = role;
    obj[QStringLiteral("content")]   = content;
    obj[QStringLiteral("timestamp")] = timestamp.toString(Qt::ISODate);
    obj[QStringLiteral("hasImage")]  = hasImage;
    return obj;
}

ChatMessage ChatMessage::fromJson(const QJsonObject &obj)
{
    ChatMessage msg;
    msg.id        = obj[QStringLiteral("id")].toString();
    msg.role      = obj[QStringLiteral("role")].toString();
    msg.content   = obj[QStringLiteral("content")].toString();
    msg.timestamp = QDateTime::fromString(obj[QStringLiteral("timestamp")].toString(), Qt::ISODate);
    msg.hasImage  = obj[QStringLiteral("hasImage")].toBool(false);
    return msg;
}

// ─────────────────────────────────────────────────────────────────────────────
//  ChatSession
// ─────────────────────────────────────────────────────────────────────────────

ChatSessionSummary ChatSession::toSummary() const
{
    ChatSessionSummary s;
    s.id           = id;
    s.title        = title;
    s.createdAt    = createdAt;
    s.updatedAt    = updatedAt;
    s.messageCount = messages.size();
    if (!messages.isEmpty()) {
        s.lastSnippet = messages.last().content.left(80);
        if (messages.last().content.size() > 80) {
            s.lastSnippet += QStringLiteral("…");
        }
    }
    return s;
}

QJsonObject ChatSession::toJson() const
{
    QJsonArray arr;
    for (const ChatMessage &msg : messages) {
        arr.append(msg.toJson());
    }

    QJsonObject obj;
    obj[QStringLiteral("id")]        = id;
    obj[QStringLiteral("title")]     = title;
    obj[QStringLiteral("createdAt")] = createdAt.toString(Qt::ISODate);
    obj[QStringLiteral("updatedAt")] = updatedAt.toString(Qt::ISODate);
    obj[QStringLiteral("messages")]  = arr;
    return obj;
}

ChatSession ChatSession::fromJson(const QJsonObject &obj)
{
    ChatSession s;
    s.id        = obj[QStringLiteral("id")].toString();
    s.title     = obj[QStringLiteral("title")].toString();
    s.createdAt = QDateTime::fromString(obj[QStringLiteral("createdAt")].toString(), Qt::ISODate);
    s.updatedAt = QDateTime::fromString(obj[QStringLiteral("updatedAt")].toString(), Qt::ISODate);

    const QJsonArray arr = obj[QStringLiteral("messages")].toArray();
    s.messages.reserve(arr.size());
    for (const QJsonValue &val : arr) {
        s.messages.append(ChatMessage::fromJson(val.toObject()));
    }
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
//  ChatHistoryManager
// ─────────────────────────────────────────────────────────────────────────────

ChatHistoryManager::ChatHistoryManager(QObject *parent)
    : QObject(parent)
{
    // Resolve storage directory
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    m_storageDir = dataDir + QStringLiteral("/conversations");
    QDir().mkpath(m_storageDir);

    // Load existing session index
    rebuildIndex();

    // If there are no sessions yet, create the first one automatically
    if (m_index.isEmpty()) {
        createSession(QStringLiteral("New Conversation"));
    } else {
        // Restore the most recent session as the current one
        switchSession(m_index.first().id);
    }
}

// ── Public: Session lifecycle ────────────────────────────────────────────────

QString ChatHistoryManager::createSession(const QString &title)
{
    ChatSession session;
    session.id        = generateId();
    session.title     = title.isEmpty() ? QStringLiteral("New Conversation") : title;
    session.createdAt = QDateTime::currentDateTime();
    session.updatedAt = session.createdAt;

    saveSession(session);
    updateIndexEntry(session);

    m_currentSessionId = session.id;
    m_currentSession   = session;

    emit sessionCreated(session.id);
    emit sessionSwitched(session.id);
    emit sessionsChanged();

    return session.id;
}

QString ChatHistoryManager::currentSessionId() const
{
    return m_currentSessionId;
}

ChatSession ChatHistoryManager::currentSession() const
{
    return m_currentSession;
}

bool ChatHistoryManager::switchSession(const QString &sessionId)
{
    // Check index
    bool found = false;
    for (const ChatSessionSummary &s : m_index) {
        if (s.id == sessionId) {
            found = true;
            break;
        }
    }
    if (!found) {
        return false;
    }

    m_currentSession   = loadSessionFromDisk(sessionId);
    m_currentSessionId = sessionId;

    emit sessionSwitched(sessionId);
    return true;
}

bool ChatHistoryManager::deleteSession(const QString &sessionId)
{
    const QString path = sessionFilePath(sessionId);
    if (!QFile::exists(path)) {
        return false;
    }

    QFile::remove(path);
    removeIndexEntry(sessionId);

    if (m_currentSessionId == sessionId) {
        m_currentSessionId.clear();
        m_currentSession = ChatSession{};

        // Switch to another session or create a fresh one
        if (!m_index.isEmpty()) {
            switchSession(m_index.first().id);
        } else {
            createSession(QStringLiteral("New Conversation"));
        }
    }

    emit sessionDeleted(sessionId);
    emit sessionsChanged();
    return true;
}

bool ChatHistoryManager::renameSession(const QString &sessionId, const QString &newTitle)
{
    // Load, rename, save
    ChatSession session = loadSessionFromDisk(sessionId);
    if (session.id.isEmpty()) {
        return false;
    }

    session.title     = newTitle.isEmpty() ? QStringLiteral("Unnamed Conversation") : newTitle;
    session.updatedAt = QDateTime::currentDateTime();
    saveSession(session);
    updateIndexEntry(session);

    if (m_currentSessionId == sessionId) {
        m_currentSession.title     = session.title;
        m_currentSession.updatedAt = session.updatedAt;
    }

    emit sessionsChanged();
    return true;
}

QList<ChatSessionSummary> ChatHistoryManager::allSessions() const
{
    return m_index;
}

int ChatHistoryManager::sessionCount() const
{
    return m_index.size();
}

void ChatHistoryManager::clearAll()
{
    QDir dir(m_storageDir);
    const QStringList files = dir.entryList({QStringLiteral("conv_*.json")}, QDir::Files);
    for (const QString &f : files) {
        QFile::remove(dir.filePath(f));
    }

    m_index.clear();
    m_currentSessionId.clear();
    m_currentSession = ChatSession{};

    // Create fresh session
    createSession(QStringLiteral("New Conversation"));

    emit sessionsChanged();
}

// ── Public: Message logging ──────────────────────────────────────────────────

void ChatHistoryManager::appendMessage(const QString &role, const QString &text, bool hasImage)
{
    if (m_currentSessionId.isEmpty()) {
        createSession(QStringLiteral("New Conversation"));
    }

    ChatMessage msg;
    msg.id        = generateId();
    msg.role      = role;
    msg.content   = text;
    msg.timestamp = QDateTime::currentDateTime();
    msg.hasImage  = hasImage;

    m_currentSession.messages.append(msg);
    m_currentSession.updatedAt = msg.timestamp;

    // Auto-title from first meaningful user message
    if (m_currentSession.title == QStringLiteral("New Conversation") &&
        role == QStringLiteral("user") && !text.trimmed().isEmpty()) {
        m_currentSession.title = generateTitle(text);
    }

    saveSession(m_currentSession);
    updateIndexEntry(m_currentSession);

    emit messageAppended(msg);
}

// ── Public: Full-text search ─────────────────────────────────────────────────

QList<SearchResult> ChatHistoryManager::search(const QString &query,
                                                const SearchFilter &filter) const
{
    QList<SearchResult> results;

    if (query.trimmed().isEmpty()) {
        return results;
    }

    // Tokenize into individual search terms (AND semantics)
    const QStringList rawTerms = query.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList terms;
    for (const QString &t : rawTerms) {
        if (!t.isEmpty()) {
            terms.append(t);
        }
    }

    // Determine which sessions to search
    QList<ChatSessionSummary> targets = m_index;
    if (!filter.sessionId.isEmpty()) {
        targets.erase(std::remove_if(targets.begin(), targets.end(),
            [&](const ChatSessionSummary &s) { return s.id != filter.sessionId; }),
            targets.end());
    }

    Qt::CaseSensitivity cs = filter.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;

    for (const ChatSessionSummary &summary : targets) {
        ChatSession session = loadSessionFromDisk(summary.id);

        for (const ChatMessage &msg : session.messages) {
            // Role filter
            if (!filter.roleFilter.isEmpty() &&
                msg.role.compare(filter.roleFilter, Qt::CaseInsensitive) != 0) {
                continue;
            }

            // Date filter
            if (filter.fromDate.isValid() && msg.timestamp < filter.fromDate) {
                continue;
            }
            if (filter.toDate.isValid() && msg.timestamp > filter.toDate) {
                continue;
            }

            // ALL terms must appear in the content (AND semantics)
            bool allMatch = true;
            int  totalCount = 0;
            for (const QString &term : terms) {
                const int occurrences = msg.content.count(term, cs);
                if (occurrences == 0) {
                    allMatch = false;
                    break;
                }
                totalCount += occurrences;
            }
            if (!allMatch) {
                continue;
            }

            SearchResult result;
            result.sessionId    = summary.id;
            result.sessionTitle = summary.title;
            result.messageId    = msg.id;
            result.role         = msg.role;
            result.timestamp    = msg.timestamp;
            result.matchCount   = totalCount;
            result.matchedSnippet = buildSnippet(msg.content, terms, filter.caseSensitive);

            // Relevance score: more matches and shorter messages rank higher
            const int len = qMax(1, msg.content.length());
            result.score = static_cast<double>(totalCount) * 100.0 / len;

            results.append(result);

            if (results.size() >= filter.maxResults) {
                goto search_done;
            }
        }
    }

search_done:
    // Sort by score descending, then by newest first
    std::sort(results.begin(), results.end(), [](const SearchResult &a, const SearchResult &b) {
        if (qAbs(a.score - b.score) > 0.0001) {
            return a.score > b.score;
        }
        return a.timestamp > b.timestamp;
    });

    return results;
}

ChatSession ChatHistoryManager::loadSession(const QString &sessionId) const
{
    return loadSessionFromDisk(sessionId);
}

// ── Public: Export ───────────────────────────────────────────────────────────

QString ChatHistoryManager::exportToMarkdown(const QString &sessionId) const
{
    ChatSession session = loadSessionFromDisk(sessionId);
    if (session.id.isEmpty()) {
        return {};
    }

    QString md;
    QTextStream out(&md);
    out << QStringLiteral("# %1\n\n").arg(session.title);
    out << QStringLiteral("*Created: %1 | Updated: %2 | Messages: %3*\n\n")
               .arg(session.createdAt.toString(QStringLiteral("yyyy-MM-dd hh:mm")))
               .arg(session.updatedAt.toString(QStringLiteral("yyyy-MM-dd hh:mm")))
               .arg(session.messages.size());
    out << QStringLiteral("---\n\n");

    for (const ChatMessage &msg : session.messages) {
        const QString timeStr = msg.timestamp.toString(QStringLiteral("hh:mm"));

        if (msg.role == QStringLiteral("user")) {
            out << QStringLiteral("**You** (%1)\n\n").arg(timeStr);
        } else if (msg.role == QStringLiteral("assistant")) {
            out << QStringLiteral("**TitanAI** (%1)\n\n").arg(timeStr);
        } else {
            out << QStringLiteral("**%1** (%2)\n\n").arg(msg.role, timeStr);
        }

        out << msg.content << QStringLiteral("\n\n");

        if (msg.hasImage) {
            out << QStringLiteral("*[Image attached]*\n\n");
        }

        out << QStringLiteral("---\n\n");
    }

    return md;
}

QString ChatHistoryManager::exportToHtml(const QString &sessionId) const
{
    ChatSession session = loadSessionFromDisk(sessionId);
    if (session.id.isEmpty()) {
        return {};
    }

    static const QString kCss = QStringLiteral(R"(
        body { font-family: 'DejaVu Sans','Inter','Segoe UI',sans-serif; color:#1f2937;
               margin:0; padding:24px; background:#ffffff; }
        h1 { font-size:22px; color:#111827; margin:0 0 4px 0; }
        .meta { font-size:12px; color:#6b7280; margin:0 0 18px 0; }
        .msg { margin:10px 0; padding:12px 14px; border-radius:6px;
               border:1px solid #e5e7eb; page-break-inside:avoid; }
        .msg-user      { background:#f0f9ff; border-left:4px solid #06b6d4; }
        .msg-assistant { background:#f5f3ff; border-left:4px solid #6366f1; }
        .msg-error     { background:#fef2f2; border-left:4px solid #ef4444; }
        .msg-tool      { background:#f8fafc; border-left:4px solid #64748b; }
        .sender { font-size:11px; font-weight:700; color:#374151; margin-bottom:4px; }
        .ts { font-weight:400; color:#9ca3af; }
        .content { font-size:13px; line-height:1.55; word-break:break-word;
                   white-space:pre-wrap; }
        .image-note { font-size:11px; color:#4f46e5; margin-top:4px; }
        .empty { color:#9ca3af; text-align:center; font-size:14px; padding:40px; }
    )");

    QString html;
    html += QStringLiteral("<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n");
    html += QStringLiteral("<title>%1</title>\n").arg(session.title.toHtmlEscaped());
    html += QStringLiteral("<style>%1</style>\n</head>\n<body>\n").arg(kCss);

    html += QStringLiteral("<h1>%1</h1>\n").arg(session.title.toHtmlEscaped());
    html += QStringLiteral(
                "<p class='meta'>%1 messages &nbsp;·&nbsp; Created %2 &nbsp;·&nbsp; Updated %3</p>\n")
                .arg(session.messages.size())
                .arg(session.createdAt.toString(QStringLiteral("yyyy-MM-dd hh:mm")))
                .arg(session.updatedAt.toString(QStringLiteral("yyyy-MM-dd hh:mm")));

    for (const ChatMessage &msg : session.messages) {
        const QString cssClass =
            msg.role == QStringLiteral("user")      ? QStringLiteral("msg-user") :
            msg.role == QStringLiteral("assistant") ? QStringLiteral("msg-assistant") :
            msg.role == QStringLiteral("error")     ? QStringLiteral("msg-error") :
            QStringLiteral("msg-tool");

        const QString roleLabel =
            msg.role == QStringLiteral("user")      ? QStringLiteral("You") :
            msg.role == QStringLiteral("assistant") ? QStringLiteral("TitanAI") :
            msg.role == QStringLiteral("error")     ? QStringLiteral("Error") :
            QStringLiteral("Tool");

        QString escapedContent = msg.content.toHtmlEscaped();

        html += QStringLiteral("<div class='msg %1'>\n").arg(cssClass);
        html += QStringLiteral(
                    "<div class='sender'>%1 <span class='ts'>· %2</span></div>\n")
                    .arg(roleLabel.toHtmlEscaped(),
                         msg.timestamp.toString(QStringLiteral("hh:mm")));
        if (!escapedContent.isEmpty()) {
            html += QStringLiteral("<div class='content'>%1</div>\n").arg(escapedContent);
        }
        if (msg.hasImage) {
            html += QStringLiteral(
                        "<div class='image-note'>📎 Image attached</div>\n");
        }
        html += QStringLiteral("</div>\n");
    }

    if (session.messages.isEmpty()) {
        html += QStringLiteral(
                    "<div class='empty'>No messages in this conversation.</div>\n");
    }

    html += QStringLiteral("</body>\n</html>\n");
    return html;
}

QString ChatHistoryManager::exportToPlainText(const QString &sessionId) const
{
    ChatSession session = loadSessionFromDisk(sessionId);
    if (session.id.isEmpty()) {
        return {};
    }

    QString text;
    QTextStream out(&text);
    out << session.title << QStringLiteral("\n");
    out << QString(session.title.size(), QLatin1Char('=')) << QStringLiteral("\n\n");

    for (const ChatMessage &msg : session.messages) {
        const QString timeStr = msg.timestamp.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
        const QString role =
            msg.role == QStringLiteral("user")      ? QStringLiteral("You") :
            msg.role == QStringLiteral("assistant") ? QStringLiteral("TitanAI") :
            msg.role;

        out << QStringLiteral("[%1] %2:\n").arg(timeStr, role);
        out << msg.content << QStringLiteral("\n\n");
    }

    return text;
}

QString ChatHistoryManager::storageDirectory() const
{
    return m_storageDir;
}

// ── Private: Persistence ─────────────────────────────────────────────────────

QString ChatHistoryManager::sessionFilePath(const QString &sessionId) const
{
    return m_storageDir + QStringLiteral("/") + sessionId + QStringLiteral(".json");
}

bool ChatHistoryManager::saveSession(const ChatSession &session) const
{
    QFile file(sessionFilePath(session.id));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QJsonDocument doc(session.toJson());
    file.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

ChatSession ChatHistoryManager::loadSessionFromDisk(const QString &sessionId) const
{
    QFile file(sessionFilePath(sessionId));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return {};
    }

    return ChatSession::fromJson(doc.object());
}

void ChatHistoryManager::rebuildIndex()
{
    m_index.clear();

    QDir dir(m_storageDir);
    const QStringList files = dir.entryList({QStringLiteral("conv_*.json")}, QDir::Files);

    for (const QString &fileName : files) {
        const QString sessionId = fileName.left(fileName.length() - 5); // strip ".json"
        ChatSession session = loadSessionFromDisk(sessionId);
        if (!session.id.isEmpty()) {
            m_index.append(session.toSummary());
        }
    }

    // Sort by most recently updated first
    std::sort(m_index.begin(), m_index.end(), [](const ChatSessionSummary &a,
                                                  const ChatSessionSummary &b) {
        return a.updatedAt > b.updatedAt;
    });
}

void ChatHistoryManager::updateIndexEntry(const ChatSession &session)
{
    const ChatSessionSummary summary = session.toSummary();

    for (ChatSessionSummary &s : m_index) {
        if (s.id == session.id) {
            s = summary;
            // Re-sort
            std::sort(m_index.begin(), m_index.end(), [](const ChatSessionSummary &a,
                                                          const ChatSessionSummary &b) {
                return a.updatedAt > b.updatedAt;
            });
            return;
        }
    }

    // Not found – insert and sort
    m_index.prepend(summary);
    std::sort(m_index.begin(), m_index.end(), [](const ChatSessionSummary &a,
                                                  const ChatSessionSummary &b) {
        return a.updatedAt > b.updatedAt;
    });
}

void ChatHistoryManager::removeIndexEntry(const QString &sessionId)
{
    m_index.erase(std::remove_if(m_index.begin(), m_index.end(),
        [&](const ChatSessionSummary &s) { return s.id == sessionId; }),
        m_index.end());
}

// ── Private: Static helpers ──────────────────────────────────────────────────

QString ChatHistoryManager::generateId()
{
    // Format: "conv_<timestamp>_<uuid-first-8-chars>"
    const QString ts  = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QString uid = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    return QStringLiteral("conv_%1_%2").arg(ts, uid);
}

QString ChatHistoryManager::generateTitle(const QString &firstMessage)
{
    QString title = firstMessage.trimmed().left(50);
    // Remove leading special chars/whitespace
    while (!title.isEmpty() && !title.at(0).isLetterOrNumber()) {
        title.remove(0, 1);
    }
    if (title.length() >= 50 && firstMessage.length() > 50) {
        title += QStringLiteral("…");
    }
    return title.isEmpty() ? QStringLiteral("New Conversation") : title;
}

QString ChatHistoryManager::buildSnippet(const QString &content, const QStringList &terms,
                                          bool caseSensitive, int contextChars)
{
    if (terms.isEmpty()) {
        return content.left(contextChars * 2);
    }

    Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;

    // Find the first occurrence of any term to anchor the snippet
    int firstPos = -1;
    for (const QString &term : terms) {
        const int pos = content.indexOf(term, 0, cs);
        if (pos >= 0 && (firstPos < 0 || pos < firstPos)) {
            firstPos = pos;
        }
    }

    if (firstPos < 0) {
        return content.left(contextChars * 2);
    }

    const int start = qMax(0, firstPos - contextChars);
    const int end   = qMin(content.length(), firstPos + contextChars + terms.first().length());
    QString snippet;
    if (start > 0) {
        snippet += QStringLiteral("…");
    }
    snippet += content.mid(start, end - start);
    if (end < content.length()) {
        snippet += QStringLiteral("…");
    }

    return highlightTerms(snippet, terms, caseSensitive);
}

QString ChatHistoryManager::highlightTerms(const QString &snippet, const QStringList &terms,
                                             bool caseSensitive)
{
    QString result = snippet.toHtmlEscaped();
    Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;

    for (const QString &term : terms) {
        // Walk the string and wrap every occurrence with <mark> ... </mark>
        int offset = 0;
        while (true) {
            const int pos = result.indexOf(term.toHtmlEscaped(), offset, cs);
            if (pos < 0) {
                break;
            }
            const QString highlight =
                QStringLiteral("<mark style='background:#f59e0b;color:#0f172a;border-radius:2px;'>"
                               "%1</mark>").arg(result.mid(pos, term.length()));
            result.replace(pos, term.length(), highlight);
            offset = pos + highlight.length();
        }
    }

    return result;
}
