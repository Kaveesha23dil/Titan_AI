#ifndef TITANAI_TEXT_EXTRACTOR_HPP
#define TITANAI_TEXT_EXTRACTOR_HPP

#include <QString>
#include <QStringList>

/**
 * @brief Discovers indexable files and extracts plain text from them.
 *
 * Plain text and source code are read directly. PDFs are converted with the
 * optional `pdftotext` (poppler) binary so no extra link-time dependency is
 * required; when it is missing the file is skipped with a clear error.
 */
class TextExtractor {
public:
    /// File extensions that are treated as indexable text/code.
    [[nodiscard]] static QStringList supportedExtensions();

    /// True when @p path has a supported extension or well-known filename.
    [[nodiscard]] static bool isSupportedFile(const QString &path);

    /**
     * Recursively collects supported files from @p roots.
     * Directories such as .git, node_modules, build and caches are skipped.
     */
    [[nodiscard]] static QStringList collectFiles(const QStringList &roots);

    /**
     * Extracts UTF-8 plain text from @p path.
     * Returns an empty string and fills @p error on failure.
     */
    [[nodiscard]] static QString extractText(const QString &path, QString *error = nullptr);

private:
    [[nodiscard]] static QString extractPdf(const QString &path, QString *error);
    [[nodiscard]] static bool isExcludedDirectory(const QString &name);
    [[nodiscard]] static bool looksBinary(const QByteArray &data);
};

#endif // TITANAI_TEXT_EXTRACTOR_HPP
