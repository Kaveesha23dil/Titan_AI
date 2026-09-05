#include "tools/web_search.hpp"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QUrlQuery>
#include <QRegularExpression>

namespace {

constexpr int kSnippetCapChars = 300;

} // namespace

WebSearch::WebSearch(QObject *parent)
    : QObject(parent)
{
    connect(&m_networkManager, &QNetworkAccessManager::finished,
            this, &WebSearch::onReplyFinished);
}

void WebSearch::search(const QString &query, int maxResults)
{
    if (m_searching || query.trimmed().isEmpty()) {
        return;
    }

    m_searching = true;
    m_results.clear();
    m_maxResults = qBound(1, maxResults, 12);

    emit searchStarted();

    // DuckDuckGo's privacy-friendly HTML endpoint. We POST the query exactly
    // like a browser would. No API key, no tracking cookies, works anonymously.
    QUrl url(QStringLiteral("https://html.duckduckgo.com/html/"));
    QUrlQuery postData;
    postData.addQueryItem(QStringLiteral("q"), query.trimmed());
    postData.addQueryItem(QStringLiteral("kl"), QStringLiteral("us-en"));
    postData.addQueryItem(QStringLiteral("kp"), QStringLiteral("-1"));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) TitanAI/%1")
                          .arg(QStringLiteral("1.0")));

    m_networkManager.post(request, postData.toString(QUrl::FullyEncoded).toUtf8());
}

void WebSearch::onReplyFinished(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        m_searching = false;
        emit searchError(QStringLiteral("Web search failed: %1").arg(reply->errorString()));
        return;
    }

    const QByteArray raw = reply->readAll();
    const QString html = QString::fromUtf8(raw);

    m_results = parseResults(html);
    m_searching = false;

    if (m_results.isEmpty()) {
        emit searchError(QStringLiteral("Web search returned no results."));
        return;
    }

    emit searchFinished(m_results);
}

QList<WebSearchResult> WebSearch::parseResults(const QString &html) const
{
    QList<WebSearchResult> results;

    // DuckDuckGo HTML results are structured as <a rel="nofollow"
    // class="result__a" href="...">Title</a> followed by
    // <a class="result__snippet" href="...">Snippet</a>.
    static const QRegularExpression resultBlockRe(
        QStringLiteral("result__a[^>]*href=\"([^\"]+)\"[^>]*>(.*?)</a>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);

    static const QRegularExpression snippetRe(
        QStringLiteral("class=\"result__snippet\"[^>]*>(.*?)</a>"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);

    static const QRegularExpression urlDecodeRe(QStringLiteral("&amp;"));

    QList<QRegularExpressionMatch> titleMatches;
    QRegularExpressionMatchIterator titleIt = resultBlockRe.globalMatch(html);
    while (titleIt.hasNext()) {
        titleMatches.append(titleIt.next());
    }

    QList<QRegularExpressionMatch> snippetMatches;
    QRegularExpressionMatchIterator snippetIt = snippetRe.globalMatch(html);
    while (snippetIt.hasNext()) {
        snippetMatches.append(snippetIt.next());
    }

    const int count = qMin(titleMatches.size(), m_maxResults);
    for (int i = 0; i < count; ++i) {
        const QRegularExpressionMatch &titleMatch = titleMatches.at(i);

        WebSearchResult result;
        result.title = unescapeEntities(stripTags(titleMatch.captured(2))).trimmed();

        QString url = titleMatch.captured(1);
        url.replace(urlDecodeRe, QStringLiteral("&"));
        url = QUrl::fromPercentEncoding(url.toUtf8());
        // DuckDuckGo wraps links in a redirect param; extract the real target.
        static const QRegularExpression redirectRe(QStringLiteral("uddg=([^&]+)"));
        const QRegularExpressionMatch redirectMatch = redirectRe.match(url);
        if (redirectMatch.hasMatch()) {
            url = QUrl::fromPercentEncoding(redirectMatch.captured(1).toUtf8());
        }
        result.url = url;

        if (i < snippetMatches.size()) {
            result.snippet = unescapeEntities(stripTags(snippetMatches.at(i).captured(1))).trimmed();
            if (result.snippet.size() > kSnippetCapChars) {
                result.snippet = result.snippet.left(kSnippetCapChars).trimmed() + QStringLiteral("…");
            }
        }

        if (result.title.isEmpty() && result.url.isEmpty()) {
            continue;
        }

        results.append(result);
    }

    return results;
}

QString WebSearch::stripTags(const QString &html) const
{
    static const QRegularExpression tagRe(QStringLiteral("<[^>]*>"));
    QString text = html;
    text.remove(tagRe);
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return text.trimmed();
}

QString WebSearch::unescapeEntities(const QString &text) const
{
    QString out = text;
    out.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    out.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    out.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    out.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    out.replace(QStringLiteral("&#x27;"), QStringLiteral("'"));
    out.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    out.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    return out;
}

QString WebSearch::formatResultsForPrompt() const
{
    if (m_results.isEmpty()) {
        return QStringLiteral("(No web search results available.)");
    }

    QString block = QStringLiteral("Web search results (grounding context):\n\n");
    int index = 1;
    for (const WebSearchResult &result : m_results) {
        block += QStringLiteral("[%1] %2\n").arg(index++).arg(result.title);
        if (!result.url.isEmpty()) {
            block += QStringLiteral("    URL: %1\n").arg(result.url);
        }
        if (!result.snippet.isEmpty()) {
            block += QStringLiteral("    %1\n").arg(result.snippet);
        }
        block += QLatin1Char('\n');
    }

    block += QStringLiteral(
        "Use the information above to give an accurate, grounded answer to the "
        "user's question. If the search results do not contain the answer, say so "
        "honestly instead of guessing.");

    return block;
}
