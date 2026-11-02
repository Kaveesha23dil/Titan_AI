#include "rag/text_extractor.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>

namespace {

// Keep individual documents bounded so a stray multi-GB file cannot exhaust RAM.
constexpr qint64 kMaxFileBytes = 4 * 1024 * 1024;
constexpr int kPdfTimeoutMs = 60000;

bool hasExtension(const QString &suffix, const QStringList &extensions)
{
    return extensions.contains(suffix, Qt::CaseInsensitive);
}

} // namespace

QStringList TextExtractor::supportedExtensions()
{
    return {
        // Documentation / notes
        QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("markdown"),
        QStringLiteral("rst"), QStringLiteral("org"), QStringLiteral("tex"),
        QStringLiteral("adoc"), QStringLiteral("asciidoc"),
        // Source code
        QStringLiteral("c"), QStringLiteral("cc"), QStringLiteral("cpp"),
        QStringLiteral("cxx"), QStringLiteral("h"), QStringLiteral("hh"),
        QStringLiteral("hpp"), QStringLiteral("hxx"), QStringLiteral("m"),
        QStringLiteral("mm"), QStringLiteral("py"), QStringLiteral("pyi"),
        QStringLiteral("js"), QStringLiteral("jsx"), QStringLiteral("ts"),
        QStringLiteral("tsx"), QStringLiteral("java"), QStringLiteral("go"),
        QStringLiteral("rs"), QStringLiteral("rb"), QStringLiteral("php"),
        QStringLiteral("cs"), QStringLiteral("swift"), QStringLiteral("kt"),
        QStringLiteral("kts"), QStringLiteral("scala"), QStringLiteral("lua"),
        QStringLiteral("pl"), QStringLiteral("r"), QStringLiteral("dart"),
        QStringLiteral("vue"), QStringLiteral("svelte"), QStringLiteral("qml"),
        QStringLiteral("sh"), QStringLiteral("bash"), QStringLiteral("zsh"),
        QStringLiteral("fish"), QStringLiteral("ps1"), QStringLiteral("bat"),
        // Markup / config / data
        QStringLiteral("html"), QStringLiteral("htm"), QStringLiteral("css"),
        QStringLiteral("scss"), QStringLiteral("sass"), QStringLiteral("less"),
        QStringLiteral("json"), QStringLiteral("yaml"), QStringLiteral("yml"),
        QStringLiteral("toml"), QStringLiteral("ini"), QStringLiteral("cfg"),
        QStringLiteral("conf"), QStringLiteral("xml"), QStringLiteral("sql"),
        QStringLiteral("graphql"), QStringLiteral("proto"), QStringLiteral("env"),
        QStringLiteral("cmake"), QStringLiteral("gradle"), QStringLiteral("properties"),
        QStringLiteral("log"), QStringLiteral("csv"), QStringLiteral("tsv"),
        // Documents
        QStringLiteral("pdf"),
    };
}

bool TextExtractor::isSupportedFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile()) {
        return false;
    }

    const QString suffix = info.suffix().toLower();
    if (!suffix.isEmpty() && hasExtension(suffix, supportedExtensions())) {
        return true;
    }

    static const QSet<QString> wellKnownNames = {
        QStringLiteral("makefile"), QStringLiteral("dockerfile"),
        QStringLiteral("readme"), QStringLiteral("license"),
        QStringLiteral("changelog"), QStringLiteral("gemfile"),
        QStringLiteral("rakefile"), QStringLiteral("procfile"),
        QStringLiteral("cmakelists.txt"), QStringLiteral("meson.build"),
    };
    return wellKnownNames.contains(info.fileName().toLower());
}

bool TextExtractor::isExcludedDirectory(const QString &name)
{
    static const QSet<QString> excluded = {
        QStringLiteral(".git"), QStringLiteral(".svn"), QStringLiteral(".hg"),
        QStringLiteral("node_modules"), QStringLiteral("build"),
        QStringLiteral("dist"), QStringLiteral("out"), QStringLiteral("target"),
        QStringLiteral(".cache"), QStringLiteral(".venv"), QStringLiteral("venv"),
        QStringLiteral("__pycache__"), QStringLiteral(".idea"), QStringLiteral(".vscode"),
        QStringLiteral(".gradle"), QStringLiteral(".m2"), QStringLiteral("vendor"),
        QStringLiteral("third_party"), QStringLiteral("third-party"),
    };
    return excluded.contains(name);
}

QStringList TextExtractor::collectFiles(const QStringList &roots)
{
    QStringList files;
    QSet<QString> seen;

    for (const QString &root : roots) {
        const QFileInfo info(root);
        if (!info.exists()) {
            continue;
        }

        if (info.isFile()) {
            const QString canonical = info.canonicalFilePath();
            if (isSupportedFile(root) && !seen.contains(canonical)) {
                seen.insert(canonical);
                files.append(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
            }
            continue;
        }

        QDirIterator it(info.absoluteFilePath(),
                        QDir::Files | QDir::NoSymLinks,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            const QFileInfo fileInfo = it.fileInfo();

            // Skip files inside excluded directories.
            bool excluded = false;
            QDir cursor(fileInfo.absolutePath());
            while (true) {
                if (isExcludedDirectory(cursor.dirName())) {
                    excluded = true;
                    break;
                }
                if (cursor.absolutePath() == info.absoluteFilePath() || !cursor.cdUp()) {
                    break;
                }
            }
            if (excluded || fileInfo.fileName().startsWith(QLatin1Char('.'))) {
                continue;
            }

            if (!isSupportedFile(path)) {
                continue;
            }

            const QString canonical = fileInfo.canonicalFilePath();
            if (canonical.isEmpty() || seen.contains(canonical)) {
                continue;
            }
            seen.insert(canonical);
            files.append(canonical);
        }
    }

    files.sort();
    return files;
}

QString TextExtractor::extractText(const QString &path, QString *error)
{
    if (error) {
        error->clear();
    }

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        if (error) {
            *error = QStringLiteral("File not found: %1").arg(path);
        }
        return {};
    }

    if (info.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0) {
        return extractPdf(path, error);
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("Could not read %1: %2").arg(path, file.errorString());
        }
        return {};
    }

    const QByteArray data = file.read(kMaxFileBytes);
    if (looksBinary(data)) {
        if (error) {
            *error = QStringLiteral("Skipped binary file: %1").arg(path);
        }
        return {};
    }

    QString text = QString::fromUtf8(data);
    // Normalise Windows line endings so line numbers stay meaningful.
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text;
}

QString TextExtractor::extractPdf(const QString &path, QString *error)
{
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("pdftotext"));
    if (executable.isEmpty()) {
        if (error) {
            *error = QStringLiteral(
                "PDF support requires poppler-utils. Install it with: sudo pacman -S poppler");
        }
        return {};
    }

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(executable, {
        QStringLiteral("-layout"),
        QStringLiteral("-enc"), QStringLiteral("UTF-8"),
        path,
        QStringLiteral("-"),
    });

    if (!process.waitForStarted(5000)) {
        if (error) {
            *error = QStringLiteral("Could not start pdftotext: %1").arg(process.errorString());
        }
        return {};
    }

    if (!process.waitForFinished(kPdfTimeoutMs)) {
        process.kill();
        process.waitForFinished();
        if (error) {
            *error = QStringLiteral("Timed out while reading PDF: %1").arg(path);
        }
        return {};
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) {
            QString detail = QString::fromUtf8(process.readAllStandardError()).trimmed();
            if (detail.isEmpty()) {
                detail = QStringLiteral("pdftotext exited with code %1").arg(process.exitCode());
            }
            *error = QStringLiteral("Could not read PDF %1: %2").arg(path, detail);
        }
        return {};
    }

    return QString::fromUtf8(process.readAllStandardOutput());
}

bool TextExtractor::looksBinary(const QByteArray &data)
{
    const int sampleSize = qMin(data.size(), 8192);
    for (int i = 0; i < sampleSize; ++i) {
        if (data.at(i) == '\0') {
            return true;
        }
    }
    return false;
}
