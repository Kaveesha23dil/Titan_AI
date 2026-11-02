#ifndef TITANAI_RAG_MANAGER_HPP
#define TITANAI_RAG_MANAGER_HPP

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

#include "rag/embedding_client.hpp"
#include "rag/text_chunker.hpp"

/**
 * @brief Local Retrieval-Augmented Generation index.
 *
 * Documents are extracted, split into overlapping chunks and embedded with an
 * Ollama embedding model. The resulting vectors are persisted as JSON under the
 * application data directory and retrieved with cosine similarity. Everything
 * runs locally; no document content leaves the machine.
 */
class RagManager : public QObject {
    Q_OBJECT

public:
    struct Chunk {
        QString source;
        QString text;
        int startLine{0};
        int endLine{0};
        QVector<float> embedding;
    };

    struct SearchHit {
        QString source;
        QString text;
        int startLine{0};
        int endLine{0};
        float score{0.0f};
    };

    explicit RagManager(QObject *parent = nullptr);
    ~RagManager() override = default;

    void setStorageDirectory(const QString &directory);
    [[nodiscard]] QString storageDirectory() const;
    void setEmbeddingModel(const QString &model);
    [[nodiscard]] QString embeddingModel() const;

    [[nodiscard]] bool isIndexing() const { return m_indexing; }
    bool isEmpty();
    int documentCount();
    int chunkCount();
    int chunkCountForSource(const QString &source);
    QStringList sources();

    void indexPaths(const QStringList &paths, bool reset = false);
    void removeSource(const QString &source);
    void clear();

    /**
     * Embeds @p query and returns the most similar chunks through @p callback.
     * The callback always runs (possibly with an empty result) so callers can
     * fall back to a normal, ungrounded answer.
     */
    void retrieve(const QString &query, int topK,
                  std::function<void(const QString &, const QList<SearchHit> &)> callback);

    [[nodiscard]] static QString formatHitsForPrompt(const QString &query,
                                                     const QList<SearchHit> &hits);

signals:
    void indexingStarted(int fileCount);
    void indexingProgress(int processed, int total, const QString &currentFile);
    void indexingFinished(int filesIndexed, int chunksIndexed);
    void indexingError(const QString &error);
    void indexChanged();
    void queryError(const QString &error);

private slots:
    void onIndexEmbeddingReady(const QVector<float> &embedding);
    void onIndexEmbeddingError(const QString &error);
    void onQueryEmbeddingReady(const QVector<float> &embedding);
    void onQueryEmbeddingError(const QString &error);

private:
    void ensureLoaded();
    bool loadIndex();
    bool saveIndex() const;
    [[nodiscard]] QString indexFilePath() const;
    [[nodiscard]] static QString defaultStorageDirectory();

    void processNextFile();
    void requestNextChunk();
    void finishIndexing();
    void abortIndexing(const QString &error);
    void removeSourceChunks(const QString &source);
    void advanceFile();

    [[nodiscard]] static float cosineSimilarity(const QVector<float> &a,
                                                const QVector<float> &b);

    QVector<Chunk> m_chunks;
    QString m_storageDirectory;
    QString m_embeddingModel{QStringLiteral("nomic-embed-text")};
    bool m_loaded{false};
    bool m_indexing{false};

    QStringList m_pendingFiles;
    int m_fileCursor{0};
    int m_filesIndexed{0};
    int m_chunksIndexedThisRun{0};
    QString m_currentFile;
    QList<TextChunk> m_currentChunks;
    int m_chunkCursor{0};
    QList<Chunk> m_currentFileChunks;

    EmbeddingClient m_indexEmbedder;
    EmbeddingClient m_queryEmbedder;

    bool m_queryBusy{false};
    int m_queryTopK{5};
    QString m_queryText;
    std::function<void(const QString &, const QList<SearchHit> &)> m_queryCallback;
};

#endif // TITANAI_RAG_MANAGER_HPP
