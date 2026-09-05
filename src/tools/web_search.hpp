#ifndef TITANAI_WEB_SEARCH_HPP
#define TITANAI_WEB_SEARCH_HPP

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QNetworkAccessManager>
#include <QMap>

class QNetworkReply;

struct WebSearchResult {
    QString title;
    QString url;
    QString snippet;
};

class WebSearch : public QObject {
    Q_OBJECT

public:
    static constexpr int kDefaultMaxResults = 6;

    explicit WebSearch(QObject *parent = nullptr);
    ~WebSearch() override = default;

    void search(const QString &query, int maxResults = 6);
    [[nodiscard]] bool isSearching() const { return m_searching; }
    [[nodiscard]] const QList<WebSearchResult> &results() const { return m_results; }

    // Formats the fetched results into a compact, model-friendly context block.
    [[nodiscard]] QString formatResultsForPrompt() const;

signals:
    void searchStarted();
    void searchFinished(const QList<WebSearchResult> &results);
    void searchError(const QString &error);

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    QList<WebSearchResult> parseResults(const QString &html) const;
    QString stripTags(const QString &html) const;
    QString unescapeEntities(const QString &text) const;

    QNetworkAccessManager m_networkManager;
    QList<WebSearchResult> m_results;
    int m_maxResults{kDefaultMaxResults};
    bool m_searching{false};
};

#endif // TITANAI_WEB_SEARCH_HPP
