#include "rag/rag_manager.hpp"

#include "rag/text_extractor.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <cmath>

RagManager::RagManager(QObject *parent)
    : QObject(parent)
    , m_storageDirectory(defaultStorageDirectory())
{
    const QString envModel = qEnvironmentVariable("TITANAI_EMBED_MODEL").trimmed();
    if (!envModel.isEmpty()) {
        m_embeddingModel = envModel;
    }

    m_indexEmbedder.setModel(m_embeddingModel);
    m_queryEmbedder.setModel(m_embeddingModel);

    connect(&m_indexEmbedder, &EmbeddingClient::embeddingReady,
            this, &RagManager::onIndexEmbeddingReady);
    connect(&m_indexEmbedder, &EmbeddingClient::embeddingError,
            this, &RagManager::onIndexEmbeddingError);
    connect(&m_queryEmbedder, &EmbeddingClient::embeddingReady,
            this, &RagManager::onQueryEmbeddingReady);
    connect(&m_queryEmbedder, &EmbeddingClient::embeddingError,
            this, &RagManager::onQueryEmbeddingError);
}

QString RagManager::defaultStorageDirectory()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
        base = QDir::homePath() + QStringLiteral("/.local/share/titani");
    }
    return base + QStringLiteral("/rag");
}

void RagManager::setStorageDirectory(const QString &directory)
{
    if (directory.trimmed().isEmpty()) {
        return;
    }
    m_storageDirectory = directory;
    m_loaded = false;
    m_chunks.clear();
}

QString RagManager::storageDirectory() const
{
    return m_storageDirectory;
}

void RagManager::setEmbeddingModel(const QString &model)
{
    const QString trimmed = model.trimmed();
    if (trimmed.isEmpty() || trimmed == m_embeddingModel) {
        return;
    }

    ensureLoaded();
    m_embeddingModel = trimmed;
    m_indexEmbedder.setModel(trimmed);
    m_queryEmbedder.setModel(trimmed);

    // Vectors from a different model are not comparable.
    if (!m_chunks.isEmpty()) {
        m_chunks.clear();
        saveIndex();
        emit indexChanged();
    }
}

QString RagManager::embeddingModel() const
{
    return m_embeddingModel;
}

QString RagManager::indexFilePath() const
{
    return m_storageDirectory + QStringLiteral("/index.json");
}

void RagManager::ensureLoaded()
{
    if (m_loaded) {
        return;
    }
    m_loaded = true;
    loadIndex();
}

bool RagManager::isEmpty()
{
    ensureLoaded();
    return m_chunks.isEmpty();
}

int RagManager::chunkCount()
{
    ensureLoaded();
    return m_chunks.size();
}

int RagManager::documentCount()
{
    ensureLoaded();
    QSet<QString> unique;
    for (const Chunk &chunk : m_chunks) {
        unique.insert(chunk.source);
    }
    return unique.size();
}

int RagManager::chunkCountForSource(const QString &source)
{
    ensureLoaded();
    int count = 0;
    for (const Chunk &chunk : m_chunks) {
        if (chunk.source == source) {
            ++count;
        }
    }
    return count;
}

QStringList RagManager::sources()
{
    ensureLoaded();
    QStringList list;
    QSet<QString> seen;
    for (const Chunk &chunk : m_chunks) {
        if (!seen.contains(chunk.source)) {
            seen.insert(chunk.source);
            list.append(chunk.source);
        }
    }
    list.sort();
    return list;
}

void RagManager::indexPaths(const QStringList &paths, bool reset)
{
    if (m_indexing) {
        emit indexingError(QStringLiteral("Indexing is already in progress."));
        return;
    }

    ensureLoaded();

    if (reset) {
        m_chunks.clear();
    }

    const QStringList files = TextExtractor::collectFiles(paths);
    if (files.isEmpty()) {
        emit indexingError(
            QStringLiteral("No supported documents were found in the selected location. "
                           "Supported types include PDF, Markdown, plain text and source code."));
        return;
    }

    m_pendingFiles = files;
    m_fileCursor = 0;
    m_filesIndexed = 0;
    m_chunksIndexedThisRun = 0;
    m_currentFile.clear();
    m_currentChunks.clear();
    m_currentFileChunks.clear();
    m_chunkCursor = 0;
    m_indexing = true;

    emit indexingStarted(files.size());
    QTimer::singleShot(0, this, &RagManager::processNextFile);
}

void RagManager::processNextFile()
{
    if (!m_indexing) {
        return;
    }

    if (m_fileCursor >= m_pendingFiles.size()) {
        finishIndexing();
        return;
    }

    m_currentFile = m_pendingFiles.at(m_fileCursor);

    QString error;
    const QString text = TextExtractor::extractText(m_currentFile, &error);

    if (!error.isEmpty() || text.trimmed().isEmpty()) {
        // Skip unreadable or empty files but keep indexing the rest.
        advanceFile();
        return;
    }

    removeSourceChunks(m_currentFile);
    m_currentChunks = TextChunker::chunkText(text);
    m_chunkCursor = 0;
    m_currentFileChunks.clear();

    if (m_currentChunks.isEmpty()) {
        advanceFile();
        return;
    }

    requestNextChunk();
}

void RagManager::requestNextChunk()
{
    if (!m_indexing) {
        return;
    }

    if (m_chunkCursor >= m_currentChunks.size()) {
        m_chunks.append(m_currentFileChunks);
        m_chunksIndexedThisRun += m_currentFileChunks.size();
        ++m_filesIndexed;
        advanceFile();
        return;
    }

    const TextChunk &chunk = m_currentChunks.at(m_chunkCursor);
    m_indexEmbedder.requestEmbedding(chunk.text);
}

void RagManager::advanceFile()
{
    m_currentChunks.clear();
    m_currentFileChunks.clear();
    m_chunkCursor = 0;
    m_currentFile.clear();
    ++m_fileCursor;

    emit indexingProgress(m_fileCursor, m_pendingFiles.size(),
                          m_fileCursor > 0 && m_fileCursor <= m_pendingFiles.size()
                              ? m_pendingFiles.at(m_fileCursor - 1)
                              : QString());

    QTimer::singleShot(0, this, &RagManager::processNextFile);
}

void RagManager::onIndexEmbeddingReady(const QVector<float> &embedding)
{
    if (!m_indexing || m_chunkCursor >= m_currentChunks.size()) {
        return;
    }

    const TextChunk &chunk = m_currentChunks.at(m_chunkCursor);

    Chunk stored;
    stored.source = m_currentFile;
    stored.text = chunk.text;
    stored.startLine = chunk.startLine;
    stored.endLine = chunk.endLine;
    stored.embedding = embedding;
    m_currentFileChunks.append(stored);

    ++m_chunkCursor;
    requestNextChunk();
}

void RagManager::onIndexEmbeddingError(const QString &error)
{
    abortIndexing(error);
}

void RagManager::finishIndexing()
{
    m_indexing = false;
    saveIndex();

    const int files = m_filesIndexed;
    const int chunks = m_chunksIndexedThisRun;
    m_pendingFiles.clear();
    m_currentFile.clear();
    m_currentChunks.clear();
    m_currentFileChunks.clear();

    emit indexingFinished(files, chunks);
    emit indexChanged();
}

void RagManager::abortIndexing(const QString &error)
{
    m_indexing = false;
    saveIndex();

    m_pendingFiles.clear();
    m_currentFile.clear();
    m_currentChunks.clear();
    m_currentFileChunks.clear();
    m_chunkCursor = 0;

    const QString hint = QStringLiteral(
        "Embedding failed: %1\n\n"
        "Make sure Ollama is running and an embedding model is installed:\n"
        "  ollama pull %2\n\n"
        "If Ollama reports that embeddings are disabled, restart the server with "
        "embeddings enabled. You can also choose another model by setting the "
        "TITANAI_EMBED_MODEL environment variable before launching TitanAI.")
        .arg(error, m_embeddingModel);

    emit indexingError(hint);
    emit indexChanged();
}

void RagManager::removeSource(const QString &source)
{
    ensureLoaded();
    removeSourceChunks(source);
    saveIndex();
    emit indexChanged();
}

void RagManager::removeSourceChunks(const QString &source)
{
    for (int i = m_chunks.size() - 1; i >= 0; --i) {
        if (m_chunks.at(i).source == source) {
            m_chunks.removeAt(i);
        }
    }
}

void RagManager::clear()
{
    ensureLoaded();
    m_chunks.clear();
    QFile::remove(indexFilePath());
    m_loaded = true;
    emit indexChanged();
}

void RagManager::retrieve(const QString &query, int topK,
                          std::function<void(const QString &, const QList<SearchHit> &)> callback)
{
    ensureLoaded();

    if (m_chunks.isEmpty() || query.trimmed().isEmpty()) {
        if (callback) {
            callback(QString(), {});
        }
        return;
    }

    if (m_queryBusy) {
        if (callback) {
            callback(QString(), {});
        }
        return;
    }

    m_queryBusy = true;
    m_queryTopK = qBound(1, topK, 10);
    m_queryText = query.trimmed();
    m_queryCallback = std::move(callback);

    m_queryEmbedder.requestEmbedding(m_queryText);
}

void RagManager::onQueryEmbeddingReady(const QVector<float> &embedding)
{
    m_queryBusy = false;

    QList<SearchHit> hits;
    hits.reserve(m_chunks.size());
    for (const Chunk &chunk : m_chunks) {
        if (chunk.embedding.size() != embedding.size()) {
            continue;
        }
        SearchHit hit;
        hit.source = chunk.source;
        hit.text = chunk.text;
        hit.startLine = chunk.startLine;
        hit.endLine = chunk.endLine;
        hit.score = cosineSimilarity(embedding, chunk.embedding);
        hits.append(hit);
    }

    std::sort(hits.begin(), hits.end(),
              [](const SearchHit &a, const SearchHit &b) { return a.score > b.score; });

    if (hits.size() > m_queryTopK) {
        hits = hits.mid(0, m_queryTopK);
    }

    const QString context = formatHitsForPrompt(m_queryText, hits);

    auto callback = std::move(m_queryCallback);
    m_queryCallback = nullptr;
    if (callback) {
        callback(context, hits);
    }
}

void RagManager::onQueryEmbeddingError(const QString &error)
{
    m_queryBusy = false;

    emit queryError(error);

    auto callback = std::move(m_queryCallback);
    m_queryCallback = nullptr;
    if (callback) {
        callback(QString(), {});
    }
}

float RagManager::cosineSimilarity(const QVector<float> &a, const QVector<float> &b)
{
    if (a.size() != b.size() || a.isEmpty()) {
        return -1.0f;
    }

    double dot = 0.0;
    double normA = 0.0;
    double normB = 0.0;
    for (int i = 0; i < a.size(); ++i) {
        dot += static_cast<double>(a.at(i)) * static_cast<double>(b.at(i));
        normA += static_cast<double>(a.at(i)) * static_cast<double>(a.at(i));
        normB += static_cast<double>(b.at(i)) * static_cast<double>(b.at(i));
    }

    if (normA <= 0.0 || normB <= 0.0) {
        return 0.0f;
    }

    return static_cast<float>(dot / (std::sqrt(normA) * std::sqrt(normB)));
}

QString RagManager::formatHitsForPrompt(const QString &query, const QList<SearchHit> &hits)
{
    if (hits.isEmpty()) {
        return {};
    }

    constexpr int kMaxContextChars = 7000;

    QString block = QStringLiteral(
        "Local knowledge base context. These excerpts were retrieved from the user's "
        "own indexed documents:\n\n");

    for (int i = 0; i < hits.size(); ++i) {
        const SearchHit &hit = hits.at(i);
        const QString name = QFileInfo(hit.source).fileName();
        const QString excerpt = QStringLiteral("[%1] %2 (lines %3-%4, relevance %5)\n%6\n\n")
                                    .arg(i + 1)
                                    .arg(name)
                                    .arg(hit.startLine)
                                    .arg(hit.endLine)
                                    .arg(QString::number(hit.score, 'f', 2))
                                    .arg(hit.text);

        if (block.size() + excerpt.size() > kMaxContextChars) {
            break;
        }
        block += excerpt;
    }

    block += QStringLiteral(
        "Answer the user's question using the context above when it is relevant. "
        "Mention the source file name(s) you relied on. If the context does not "
        "contain the answer, say so clearly and answer from general knowledge "
        "instead of inventing details.");

    Q_UNUSED(query);
    return block;
}

bool RagManager::saveIndex() const
{
    if (m_storageDirectory.trimmed().isEmpty()) {
        return false;
    }

    if (!QDir().mkpath(m_storageDirectory)) {
        return false;
    }

    QJsonArray chunkArray;
    for (const Chunk &chunk : m_chunks) {
        QJsonArray embeddingArray;
        for (float value : chunk.embedding) {
            embeddingArray.append(static_cast<double>(value));
        }

        QJsonObject obj;
        obj[QStringLiteral("source")] = chunk.source;
        obj[QStringLiteral("text")] = chunk.text;
        obj[QStringLiteral("startLine")] = chunk.startLine;
        obj[QStringLiteral("endLine")] = chunk.endLine;
        obj[QStringLiteral("embedding")] = embeddingArray;
        chunkArray.append(obj);
    }

    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("embeddingModel")] = m_embeddingModel;
    root[QStringLiteral("chunks")] = chunkArray;

    QSaveFile file(indexFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return file.commit();
}

bool RagManager::loadIndex()
{
    QFile file(indexFilePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    const QJsonObject root = doc.object();
    const QString storedModel = root.value(QStringLiteral("embeddingModel")).toString();
    if (!storedModel.isEmpty() && storedModel != m_embeddingModel) {
        // Embeddings from another model are not comparable; a reindex is required.
        return false;
    }

    m_chunks.clear();
    const QJsonArray chunkArray = root.value(QStringLiteral("chunks")).toArray();
    m_chunks.reserve(chunkArray.size());

    for (const QJsonValue &value : chunkArray) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();

        Chunk chunk;
        chunk.source = obj.value(QStringLiteral("source")).toString();
        chunk.text = obj.value(QStringLiteral("text")).toString();
        chunk.startLine = obj.value(QStringLiteral("startLine")).toInt(1);
        chunk.endLine = obj.value(QStringLiteral("endLine")).toInt(1);

        const QJsonArray embeddingArray = obj.value(QStringLiteral("embedding")).toArray();
        chunk.embedding.reserve(embeddingArray.size());
        for (const QJsonValue &entry : embeddingArray) {
            chunk.embedding.append(static_cast<float>(entry.toDouble()));
        }

        if (!chunk.source.isEmpty() && !chunk.text.isEmpty() && !chunk.embedding.isEmpty()) {
            m_chunks.append(chunk);
        }
    }

    return true;
}
