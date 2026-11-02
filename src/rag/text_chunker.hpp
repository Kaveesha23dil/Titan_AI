#ifndef TITANAI_TEXT_CHUNKER_HPP
#define TITANAI_TEXT_CHUNKER_HPP

#include <QList>
#include <QString>

struct TextChunk {
    QString text;
    int startLine{1};
    int endLine{1};
};

/**
 * @brief Splits a document into overlapping chunks suitable for embedding.
 *
 * Chunks target @p maxChars characters, preferring to break on a newline and
 * falling back to a space so code and prose both stay readable. A small overlap
 * preserves context across chunk boundaries.
 */
class TextChunker {
public:
    [[nodiscard]] static QList<TextChunk> chunkText(const QString &text,
                                                    int maxChars = 900,
                                                    int overlap = 150);
};

#endif // TITANAI_TEXT_CHUNKER_HPP
