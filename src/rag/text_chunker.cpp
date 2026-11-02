#include "rag/text_chunker.hpp"

#include <QVector>

#include <algorithm>

QList<TextChunk> TextChunker::chunkText(const QString &text, int maxChars, int overlap)
{
    QList<TextChunk> chunks;
    if (text.trimmed().isEmpty()) {
        return chunks;
    }

    maxChars = qBound(200, maxChars, 4000);
    overlap = qBound(0, overlap, maxChars / 2);

    // Offsets at which each line starts, used to report line ranges.
    QVector<int> lineStarts;
    lineStarts.append(0);
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('\n')) {
            lineStarts.append(i + 1);
        }
    }

    const int length = text.size();
    int start = 0;
    while (start < length) {
        int end = qMin(start + maxChars, length);

        if (end < length) {
            const int minBreak = start + maxChars / 2;

            const int newline = text.lastIndexOf(QLatin1Char('\n'), end - 1);
            if (newline >= minBreak) {
                end = newline + 1;
            } else {
                const int space = text.lastIndexOf(QLatin1Char(' '), end - 1);
                if (space >= minBreak) {
                    end = space + 1;
                }
            }
        }

        const QString piece = text.mid(start, end - start).trimmed();
        if (!piece.isEmpty()) {
            const int startIndex = static_cast<int>(
                std::upper_bound(lineStarts.begin(), lineStarts.end(), start) - lineStarts.begin()) - 1;
            const int endIndex = static_cast<int>(
                std::upper_bound(lineStarts.begin(), lineStarts.end(), qMax(start, end - 1)) - lineStarts.begin()) - 1;

            TextChunk chunk;
            chunk.text = piece;
            chunk.startLine = qMax(1, startIndex + 1);
            chunk.endLine = qMax(chunk.startLine, endIndex + 1);
            chunks.append(chunk);
        }

        if (end >= length) {
            break;
        }

        int next = end - overlap;
        if (next <= start) {
            next = end;
        }
        start = next;
    }

    return chunks;
}
