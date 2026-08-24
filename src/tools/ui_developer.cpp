#include "tools/ui_developer.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>

UiDeveloper::UiDeveloper(QObject *parent)
    : QObject(parent)
{
}

// ─────────────────────────────────────────────────────────────
//  Framework Detection
// ─────────────────────────────────────────────────────────────
UiDeveloper::Framework UiDeveloper::detectFramework(const QString &projectDirectory)
{
    QDir dir(projectDirectory);
    if (dir.exists(QStringLiteral("CMakeLists.txt")) ||
        dir.exists(QStringLiteral("*.pro"))) {
        return Framework::QtCpp;
    }
    if (dir.exists(QStringLiteral("pubspec.yaml"))) {
        return Framework::Flutter;
    }
    if (QFile::exists(dir.filePath(QStringLiteral("package.json")))) {
        // Distinguish React / Vue / plain JS by inspecting package.json
        QFile pkgFile(dir.filePath(QStringLiteral("package.json")));
        if (pkgFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString content = QString::fromUtf8(pkgFile.readAll());
            pkgFile.close();
            if (content.contains(QStringLiteral("\"react\"")) ||
                content.contains(QStringLiteral("\"@react"))) {
                return Framework::React;
            }
            if (content.contains(QStringLiteral("\"vue\"")) ||
                content.contains(QStringLiteral("\"@vue"))) {
                return Framework::Vue;
            }
        }
        return Framework::HtmlCssJs;
    }
    if (dir.exists(QStringLiteral("requirements.txt")) ||
        dir.exists(QStringLiteral("setup.py")) ||
        dir.exists(QStringLiteral("pyproject.toml"))) {
        return Framework::Python;
    }
    // Default to HTML/CSS/JS for empty or unknown directories
    return Framework::HtmlCssJs;
}

QString UiDeveloper::frameworkName(Framework fw)
{
    switch (fw) {
    case Framework::HtmlCssJs: return QStringLiteral("HTML / CSS / JavaScript");
    case Framework::React:     return QStringLiteral("React");
    case Framework::Vue:       return QStringLiteral("Vue.js");
    case Framework::QtCpp:     return QStringLiteral("Qt 6 C++");
    case Framework::Flutter:   return QStringLiteral("Flutter / Dart");
    case Framework::Python:    return QStringLiteral("Python (Tkinter / PyQt)");
    default:                   return QStringLiteral("Auto-detected");
    }
}

// ─────────────────────────────────────────────────────────────
//  Prompt Builder
// ─────────────────────────────────────────────────────────────
QString UiDeveloper::buildUiGenerationPrompt(const QString &requirements,
                                             Framework framework,
                                             const QString &projectDirectory)
{
    const QString fwName = frameworkName(framework);
    QDir dir(projectDirectory);

    // Collect existing filenames for context
    QStringList existingFiles;
    const QFileInfoList entries = dir.entryInfoList(
        QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &fi : entries) {
        existingFiles << fi.fileName();
    }

    QString prompt;
    prompt += QStringLiteral(
        "You are an expert %1 UI developer. The user wants you to implement the following UI:\n\n"
        "USER REQUIREMENTS:\n%2\n\n"
    ).arg(fwName, requirements);

    if (!existingFiles.isEmpty()) {
        prompt += QStringLiteral("EXISTING FILES IN PROJECT:\n%1\n\n").arg(existingFiles.join(QStringLiteral(", ")));
    }

    prompt += QStringLiteral(
        "Generate COMPLETE, PRODUCTION-READY source files for the described UI.\n\n"
        "Output ONLY file blocks in this exact format:\n"
        "FILE: <relative/path/to/file>\n"
        "<complete file content — no truncation, no placeholders>\n"
        "ENDFILE\n\n"
        "Rules:\n"
        "- Include ALL necessary files (markup, styles, scripts, components).\n"
        "- Use modern, premium dark-theme design with smooth animations where appropriate.\n"
        "- Do NOT include any explanation, markdown fences, or text outside FILE/ENDFILE blocks.\n"
        "- Each FILE block must be self-contained and immediately usable.\n"
        "- Adapt file paths to match the %1 project conventions.\n"
    ).arg(fwName);

    return prompt;
}

// ─────────────────────────────────────────────────────────────
//  LLM Output Parser
// ─────────────────────────────────────────────────────────────
QList<UiDeveloper::GeneratedFile> UiDeveloper::parseGeneratedFiles(const QString &llmOutput)
{
    QList<GeneratedFile> files;
    QSet<QString> seenPaths;

    auto addFile = [&](const QString &path, const QString &content) {
        QString cleanPath = path.trimmed();
        // Remove surrounding quotes, markdown backticks or comment characters if any
        if ((cleanPath.startsWith(QLatin1Char('"')) && cleanPath.endsWith(QLatin1Char('"'))) ||
            (cleanPath.startsWith(QLatin1Char('`')) && cleanPath.endsWith(QLatin1Char('`')))) {
            cleanPath = cleanPath.mid(1, cleanPath.length() - 2).trimmed();
        }
        while (cleanPath.startsWith(QStringLiteral("//")) || cleanPath.startsWith(QStringLiteral("#")) ||
               cleanPath.startsWith(QStringLiteral("/*"))) {
            if (cleanPath.startsWith(QStringLiteral("//")) || cleanPath.startsWith(QStringLiteral("/*"))) {
                cleanPath = cleanPath.mid(2).trimmed();
            } else if (cleanPath.startsWith(QStringLiteral("#"))) {
                cleanPath = cleanPath.mid(1).trimmed();
            }
        }
        if (cleanPath.endsWith(QStringLiteral("*/"))) {
            cleanPath = cleanPath.left(cleanPath.length() - 2).trimmed();
        }

        QString cleanContent = content.trimmed();
        if (!cleanPath.isEmpty() && !cleanContent.isEmpty() && !seenPaths.contains(cleanPath)) {
            seenPaths.insert(cleanPath);
            files.append(GeneratedFile{cleanPath, cleanContent});
        }
    };

    // 1. ENDFILE-delimited variant
    static const QRegularExpression endfileRe(
        QStringLiteral("(?:===+)?\\s*FILE:\\s*([^\\n]+)\\n([\\s\\S]*?)(?:ENDFILE|===+)"),
        QRegularExpression::MultilineOption);

    auto endIt = endfileRe.globalMatch(llmOutput);
    while (endIt.hasNext()) {
        auto m = endIt.next();
        addFile(m.captured(1), m.captured(2));
    }

    // 2. Sequential FILE: markers
    if (files.isEmpty()) {
        static const QRegularExpression fileRe(
            QStringLiteral("(?:^|\\n)(?:===+)?\\s*FILE:\\s*([^\\n]+)\\n([\\s\\S]*?)(?=(?:\\n(?:===+)?\\s*FILE:)|$)"),
            QRegularExpression::MultilineOption);
        auto fIt = fileRe.globalMatch(llmOutput);
        while (fIt.hasNext()) {
            auto m = fIt.next();
            addFile(m.captured(1), m.captured(2));
        }
    }

    // 3. Fallback: markdown code blocks with filename comment or tag
    if (files.isEmpty()) {
        static const QRegularExpression mdRe(
            QStringLiteral("```(?:[\\w+-]+)?(?:\\s*\\n|\\s+([^\\n]+)\\n)(?:(?:\\/\\/|#|<!--)\\s*([^\\n]+?)(?:\\s*-->)?\\n)?([\\s\\S]*?)```"),
            QRegularExpression::MultilineOption);
        auto mdIt = mdRe.globalMatch(llmOutput);
        int idx = 0;
        while (mdIt.hasNext()) {
            auto m = mdIt.next();
            QString path = m.captured(1).trimmed();
            if (path.isEmpty()) {
                path = m.captured(2).trimmed();
            }
            if (path.isEmpty()) {
                path = QStringLiteral("ui_output_%1.txt").arg(++idx);
            }
            addFile(path, m.captured(3));
        }
    }

    return files;
}

// ─────────────────────────────────────────────────────────────
//  Git Helpers
// ─────────────────────────────────────────────────────────────
bool UiDeveloper::gitRun(const QString &workdir, const QStringList &args,
                          QString *output) const
{
    QProcess proc;
    proc.setWorkingDirectory(workdir);
    proc.start(QStringLiteral("git"), args);
    proc.waitForFinished(10000);
    if (output) {
        *output = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    }
    return proc.exitCode() == 0;
}

bool UiDeveloper::branchExists(const QString &workdir, const QString &branch) const
{
    QString out;
    gitRun(workdir, {QStringLiteral("branch"), QStringLiteral("--list"), branch}, &out);
    return !out.isEmpty();
}

QString UiDeveloper::currentBranch(const QString &workdir) const
{
    QString out;
    gitRun(workdir,
           {QStringLiteral("rev-parse"), QStringLiteral("--abbrev-ref"), QStringLiteral("HEAD")},
           &out);
    return out;
}

bool UiDeveloper::createAndCheckoutBranch(const QString &workdir, const QString &branch)
{
    if (branchExists(workdir, branch)) {
        // Branch already exists — check it out
        return gitRun(workdir, {QStringLiteral("checkout"), branch});
    }
    return gitRun(workdir, {QStringLiteral("checkout"), QStringLiteral("-b"), branch});
}

bool UiDeveloper::commitFiles(const QString &workdir, const QStringList &files,
                               const QString &message)
{
    // Stage specific files
    QStringList addArgs = {QStringLiteral("add"), QStringLiteral("--")};
    addArgs << files;
    if (!gitRun(workdir, addArgs)) {
        return false;
    }
    return gitRun(workdir, {QStringLiteral("commit"), QStringLiteral("-m"), message});
}

// ─────────────────────────────────────────────────────────────
//  Main Entry Point
// ─────────────────────────────────────────────────────────────
void UiDeveloper::implementFromLlmOutput(const QString &llmOutput,
                                          const QString &projectDirectory,
                                          const QString &branchName)
{
    m_busy = true;

    emit progress(QStringLiteral("Parsing generated files from AI response..."));

    const QList<GeneratedFile> files = parseGeneratedFiles(llmOutput);
    if (files.isEmpty()) {
        m_busy = false;
        emit finished(false,
            QStringLiteral("No parseable FILE blocks found in the AI response. "
                           "Try rephrasing your requirements or checking the model output."),
            QString());
        return;
    }

    emit progress(QStringLiteral("Found %1 file(s) to generate.").arg(files.size()));

    // ── Git branch ──────────────────────────────────────────
    // Check git availability
    QString gitVersion;
    if (!gitRun(projectDirectory, {QStringLiteral("--version")}, &gitVersion)) {
        // Git not available — write files without branching
        emit progress(QStringLiteral("Note: git not found. Writing files without branch management."));
    } else {
        emit progress(QStringLiteral("Creating git branch: %1").arg(branchName));
        if (!createAndCheckoutBranch(projectDirectory, branchName)) {
            m_busy = false;
            emit finished(false,
                QStringLiteral("Failed to create git branch '%1'. "
                               "Ensure the project directory is a git repository.").arg(branchName),
                QString());
            return;
        }
        emit progress(QStringLiteral("Switched to branch: %1").arg(branchName));
    }

    // ── Write files ─────────────────────────────────────────
    QDir projectDir(projectDirectory);
    QStringList writtenRelPaths;
    QStringList skipped;

    for (const GeneratedFile &gf : files) {
        const QString absPath = projectDir.filePath(gf.relativePath);
        QFileInfo fi(absPath);
        QDir().mkpath(fi.absolutePath());

        // Backup existing file
        if (fi.exists()) {
            QFile::copy(absPath, absPath + QStringLiteral(".bak"));
        }

        QFile out(absPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            skipped << gf.relativePath;
            emit progress(QStringLiteral("⚠ Could not write: %1").arg(gf.relativePath));
            continue;
        }
        QTextStream ts(&out);
        ts << gf.content;
        out.close();

        emit progress(QStringLiteral("✓ Written: %1").arg(gf.relativePath));
        writtenRelPaths << gf.relativePath;
    }

    if (writtenRelPaths.isEmpty()) {
        m_busy = false;
        emit finished(false,
            QStringLiteral("All files failed to write:\n  %1").arg(skipped.join(QStringLiteral("\n  "))),
            branchName);
        return;
    }

    // ── Git commit ──────────────────────────────────────────
    const QString commitMsg = QStringLiteral("feat(ui): AI-generated UI implementation\n\nGenerated files:\n  ")
                              + writtenRelPaths.join(QStringLiteral("\n  "));

    bool committed = false;
    if (!gitVersion.isEmpty()) {
        emit progress(QStringLiteral("Committing %1 file(s) to '%2'...").arg(writtenRelPaths.size()).arg(branchName));
        committed = commitFiles(projectDirectory, writtenRelPaths, commitMsg);
        if (committed) {
            emit progress(QStringLiteral("✓ Commit created on branch '%1' (not pushed).").arg(branchName));
        } else {
            emit progress(QStringLiteral("⚠ Could not commit — files are still written to disk."));
        }
    }

    // ── Summary ─────────────────────────────────────────────
    QString summary;
    summary += QStringLiteral("**UI Implementation Complete** on branch `%1`\n\n").arg(branchName);
    summary += QStringLiteral("**Generated %1 file(s):**\n").arg(writtenRelPaths.size());
    for (const QString &p : writtenRelPaths) {
        summary += QStringLiteral("  • %1\n").arg(p);
    }
    if (!skipped.isEmpty()) {
        summary += QStringLiteral("\n**Skipped (write error):**\n");
        for (const QString &p : skipped) {
            summary += QStringLiteral("  • %1\n").arg(p);
        }
    }
    if (committed) {
        summary += QStringLiteral("\nChanges are committed locally on branch `%1`. "
                                  "Review and push when ready.").arg(branchName);
    } else {
        summary += QStringLiteral("\nFiles written to disk. Git commit was skipped — "
                                  "please commit manually when ready.");
    }

    m_busy = false;
    emit finished(true, summary, branchName);
}
