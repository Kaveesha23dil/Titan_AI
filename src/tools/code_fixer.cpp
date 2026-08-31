#include "tools/code_fixer.hpp"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace {

constexpr int kMaxErrors = 20;
constexpr int kContextRadius = 6;
constexpr int kMaxContextLines = 200;
constexpr int kBuildTimeoutMs = 180000;

} // namespace

CodeFixer::CodeFixer(QObject *parent)
    : QObject(parent)
    , m_buildTimeout(new QTimer(this))
{
    m_buildTimeout->setSingleShot(true);
    connect(m_buildTimeout, &QTimer::timeout, this, &CodeFixer::onBuildTimeout);
}

CodeFixer::~CodeFixer()
{
    if (m_buildProcess && m_buildProcess->state() != QProcess::NotRunning) {
        m_buildProcess->kill();
        m_buildProcess->waitForFinished(3000);
    }
    delete m_buildProcess;
}

bool CodeFixer::isBusy() const
{
    return m_buildProcess && m_buildProcess->state() != QProcess::NotRunning;
}

QList<CodeFixer::BuildError> CodeFixer::parseErrors(const QString &output)
{
    QList<BuildError> errors;
    QSet<QString> seen;

    static const QRegularExpression gccWithColRe(
        QStringLiteral("(?:^|\\s)([\\w./@+~-]+\\.[a-zA-Z0-9]+):(\\d+):(\\d+):\\s*(?:fatal\\s+)?error:\\s*(.*)$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression gccNoColRe(
        QStringLiteral("(?:^|\\s)([\\w./@+~-]+\\.[a-zA-Z0-9]+):(\\d+):\\s*(?:fatal\\s+)?error:\\s*(.*)$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tsRe(
        QStringLiteral("(?:^|\\s)([\\w./@+~-]+\\.[a-zA-Z0-9]+)\\((\\d+)(?:,(\\d+))?\\):\\s*(?:error\\s+)?(TS\\d+:.*)$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression rustErrRe(
        QStringLiteral("^\\s*error\\s*(\\[[^]]+\\])?:\\s*(.*)$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression rustArrowRe(
        QStringLiteral("^\\s*--\\>\\s*(.+):(\\d+):(\\d+)\\s*$"));

    auto addError = [&](const QString &file, int line, int col, const QString &message) {
        if (line <= 0) {
            return;
        }
        const QString key = file + QLatin1Char(':') + QString::number(line) + QLatin1Char(':') + message;
        if (seen.contains(key)) {
            return;
        }
        seen.insert(key);
        errors.append(BuildError{file, line, col, message.trimmed()});
    };

    QString rustMessage;
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString trimmed = rawLine.trimmed();

        QRegularExpressionMatch rustErrMatch = rustErrRe.match(trimmed);
        if (rustErrMatch.hasMatch()) {
            const QString code = rustErrMatch.captured(1);
            const QString text = rustErrMatch.captured(2);
            rustMessage = code.isEmpty() ? text : code + QStringLiteral(": ") + text;
        }
        if (!rustMessage.isEmpty()) {
            QRegularExpressionMatch arrowMatch = rustArrowRe.match(trimmed);
            if (arrowMatch.hasMatch()) {
                addError(arrowMatch.captured(1),
                         arrowMatch.captured(2).toInt(),
                         arrowMatch.captured(3).toInt(),
                         rustMessage);
                rustMessage.clear();
            }
        }

        QRegularExpressionMatch withColMatch = gccWithColRe.match(rawLine);
        if (withColMatch.hasMatch()) {
            addError(withColMatch.captured(1),
                     withColMatch.captured(2).toInt(),
                     withColMatch.captured(3).toInt(),
                     withColMatch.captured(4));
            continue;
        }

        QRegularExpressionMatch tsMatch = tsRe.match(rawLine);
        if (tsMatch.hasMatch()) {
            addError(tsMatch.captured(1),
                     tsMatch.captured(2).toInt(),
                     tsMatch.captured(3).isEmpty() ? 0 : tsMatch.captured(3).toInt(),
                     tsMatch.captured(4));
            continue;
        }

        QRegularExpressionMatch noColMatch = gccNoColRe.match(rawLine);
        if (noColMatch.hasMatch()) {
            addError(noColMatch.captured(1), noColMatch.captured(2).toInt(), 0, noColMatch.captured(3));
        }
    }

    while (errors.size() > kMaxErrors) {
        errors.removeLast();
    }

    return errors;
}

QString CodeFixer::resolveProjectRoot(const QString &startDir)
{
    static const QStringList markers = {
        QStringLiteral(".git"),         QStringLiteral("CMakeLists.txt"),
        QStringLiteral("package.json"), QStringLiteral("Cargo.toml"),
        QStringLiteral("Makefile"),     QStringLiteral("meson.build"),
        QStringLiteral("pyproject.toml"), QStringLiteral("pom.xml"),
        QStringLiteral("build.gradle"), QStringLiteral("go.mod"),
        QStringLiteral("composer.json"),
    };

    const QString base = startDir.isEmpty() ? QDir::currentPath() : startDir;
    QDir dir(base);
    for (int depth = 0; depth < 12; ++depth) {
        for (const QString &marker : markers) {
            if (dir.exists(marker)) {
                return dir.absolutePath();
            }
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QDir(base).absolutePath();
}

QString CodeFixer::readContextForFile(const QString &absoluteFilePath,
                                      const QList<BuildError> &fileErrors)
{
    QFile file(absoluteFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QStringLiteral("=== FILE: %1 ===\n// (could not read file)\n").arg(absoluteFilePath);
    }
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    file.close();

    QList<QPair<int, int>> ranges;
    for (const BuildError &error : fileErrors) {
        // Use 64-bit arithmetic so an adversarially large parsed line number
        // (up to INT_MAX) cannot overflow when adding the context radius.
        const qint64 lo = qMax<qint64>(1, qint64(error.line) - kContextRadius);
        const qint64 hi = qMin<qint64>(lines.size(), qint64(error.line) + kContextRadius);
        ranges.append({int(lo), int(hi)});
    }
    std::sort(ranges.begin(), ranges.end());

    QList<QPair<int, int>> merged;
    for (const auto &range : ranges) {
        if (merged.isEmpty() || range.first > merged.last().second + 1) {
            merged.append(range);
        } else {
            merged.last().second = qMax(merged.last().second, range.second);
        }
    }

    QString out = QStringLiteral("=== FILE: %1 ===\n").arg(absoluteFilePath);
    int total = 0;
    for (const auto &range : merged) {
        for (int i = range.first; i <= range.second && total < kMaxContextLines; ++i, ++total) {
            const QString line = (i - 1 < lines.size()) ? lines.at(i - 1) : QString();
            out += QStringLiteral("%1: %2\n").arg(i, 4).arg(line);
        }
        if (total < kMaxContextLines) {
            out += QLatin1Char('\n');
        }
    }
    return out;
}

QList<CodeFixer::FixEdit> CodeFixer::parseFixes(const QString &llmOutput)
{
    QList<FixEdit> fixes;

    static const QRegularExpression fileRe(
        QStringLiteral("^FILE:\\s*(.+?)\\s*$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression rangeRe(
        QStringLiteral("^REPLACE_LINES:\\s*(\\d+)\\s*(?:[-:]|to|,)\\s*(\\d+)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression rangeSingleRe(
        QStringLiteral("^REPLACE_LINES:\\s*(\\d+)\\s*$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression endRe(
        QStringLiteral("^END\\s*$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression fenceRe(QStringLiteral("^```+"));

    QString currentFile;
    int startLine = 0;
    int endLine = 0;
    QStringList block;

    auto flush = [&]() {
        if (!currentFile.isEmpty() && startLine >= 1 && endLine >= startLine) {
            QString replacement = block.join(QLatin1Char('\n'));
            while (replacement.endsWith(QLatin1Char('\n'))) {
                replacement.chop(1);
            }
            while (replacement.startsWith(QLatin1Char('\n'))) {
                replacement.remove(0, 1);
            }
            fixes.append(FixEdit{currentFile, startLine, endLine, replacement});
        }
        currentFile.clear();
        startLine = 0;
        endLine = 0;
        block.clear();
    };

    const QStringList lines = llmOutput.split(QLatin1Char('\n'));
    for (QString line : lines) {
        const QString trimmed = line.trimmed();

        QRegularExpressionMatch fileMatch = fileRe.match(trimmed);
        if (fileMatch.hasMatch()) {
            flush();
            currentFile = fileMatch.captured(1);
            continue;
        }

        QRegularExpressionMatch rangeMatch = rangeRe.match(trimmed);
        if (!rangeMatch.hasMatch()) {
            rangeMatch = rangeSingleRe.match(trimmed);
        }
        if (rangeMatch.hasMatch()) {
            startLine = rangeMatch.captured(1).toInt();
            endLine = rangeMatch.hasCaptured(2) ? rangeMatch.captured(2).toInt() : startLine;
            continue;
        }

        if (endRe.match(trimmed).hasMatch()) {
            flush();
            continue;
        }

        if (fenceRe.match(trimmed).hasMatch()) {
            continue;
        }

        if (!currentFile.isEmpty()) {
            block.append(line);
        }
    }
    flush();

    return fixes;
}

bool CodeFixer::applyEdit(const QString &absoluteFilePath, const FixEdit &edit, QString *errorOut)
{
    auto fail = [&](const QString &message) {
        if (errorOut) {
            *errorOut = message;
        }
        return false;
    };

    if (edit.startLine < 1 || edit.endLine < edit.startLine) {
        return fail(QStringLiteral("invalid line range %1-%2").arg(edit.startLine).arg(edit.endLine));
    }

    QFile file(absoluteFilePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return fail(QStringLiteral("cannot read '%1'").arg(absoluteFilePath));
    }
    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    const bool hadTrailingNewline = content.endsWith(QLatin1Char('\n'));
    QStringList lines = content.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty()) {
        lines.removeLast();
    }

    if (edit.startLine > lines.size()) {
        return fail(QStringLiteral("'%1' has %2 lines; cannot edit line %3")
                        .arg(absoluteFilePath)
                        .arg(lines.size())
                        .arg(edit.startLine));
    }

    const int end = qMin(edit.endLine, lines.size());
    const int startIdx = edit.startLine - 1;
    const QStringList replacementLines = edit.replacement.split(QLatin1Char('\n'));

    QStringList newLines;
    newLines.reserve(lines.size() - (end - startIdx) + replacementLines.size());
    newLines.append(lines.mid(0, startIdx));
    newLines.append(replacementLines);
    newLines.append(lines.mid(end));

    QFile::remove(absoluteFilePath + QStringLiteral(".bak"));
    QFile::copy(absoluteFilePath, absoluteFilePath + QStringLiteral(".bak"));

    QString newContent = newLines.join(QLatin1Char('\n'));
    if (hadTrailingNewline) {
        newContent += QLatin1Char('\n');
    }

    QFile outFile(absoluteFilePath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return fail(QStringLiteral("cannot write '%1'").arg(absoluteFilePath));
    }
    outFile.write(newContent.toUtf8());
    outFile.close();

    return true;
}

void CodeFixer::runBuild(const QString &workdir, const QString &command)
{
    if (isBusy()) {
        emit buildFinished(false, QStringLiteral("A build is already running."));
        return;
    }
    if (command.trimmed().isEmpty()) {
        emit buildFinished(false, QStringLiteral("No build command was configured."));
        return;
    }
    if (!workdir.isEmpty() && !QDir(workdir).exists()) {
        emit buildFinished(false,
                           QStringLiteral("Project directory does not exist: %1").arg(workdir));
        return;
    }

    m_buildLog.clear();
    m_buildDone = false;

    m_buildProcess = new QProcess(this);
    m_buildProcess->setProgram(QStringLiteral("sh"));
    m_buildProcess->setArguments({QStringLiteral("-c"), command});
    if (!workdir.isEmpty()) {
        m_buildProcess->setWorkingDirectory(workdir);
    }

    connect(m_buildProcess, &QProcess::readyReadStandardOutput, this, &CodeFixer::readBuildOutput);
    connect(m_buildProcess, &QProcess::readyReadStandardError, this, &CodeFixer::readBuildOutput);
    connect(m_buildProcess, &QProcess::finished, this, &CodeFixer::onBuildFinished);

    m_buildTimeout->start(kBuildTimeoutMs);
    m_buildProcess->start();
}

void CodeFixer::readBuildOutput()
{
    if (!m_buildProcess) {
        return;
    }
    m_buildLog += m_buildProcess->readAllStandardOutput();
    m_buildLog += m_buildProcess->readAllStandardError();
    if (m_buildLog.size() > 2 * 1024 * 1024) {
        m_buildLog = m_buildLog.right(1024 * 1024);
    }
}

void CodeFixer::onBuildFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    m_buildTimeout->stop();
    if (m_buildDone) {
        return;
    }
    m_buildDone = true;

    const bool success = (exitStatus == QProcess::NormalExit && exitCode == 0);
    const QString output = QString::fromUtf8(m_buildLog);

    if (m_buildProcess) {
        m_buildProcess->deleteLater();
        m_buildProcess = nullptr;
    }

    emit buildFinished(success, output);
}

void CodeFixer::onBuildTimeout()
{
    if (m_buildDone || !m_buildProcess) {
        return;
    }
    if (m_buildProcess->state() != QProcess::NotRunning) {
        m_buildProcess->kill();
    }
    // Clean up the process here so a later `finished` signal (which
    // onBuildFinished will skip because m_buildDone is true) does not leak it.
    m_buildProcess->deleteLater();
    m_buildProcess = nullptr;
    m_buildDone = true;
    emit buildFinished(false,
                       QStringLiteral("Build timed out after %1 seconds.")
                           .arg(kBuildTimeoutMs / 1000));
}
