#ifndef TITANAI_UI_DEVELOPER_HPP
#define TITANAI_UI_DEVELOPER_HPP

#include <QObject>
#include <QString>
#include <QStringList>
#include <QImage>

// ─────────────────────────────────────────────────────────────
//  UiDeveloper
//
//  Implements the "UI Design → Code" pipeline:
//    1. Detects the project framework (Qt/C++, HTML/JS, React, Flutter, …)
//    2. Creates a local git branch (never pushes to remote)
//    3. Generates and writes production-ready UI source files
//    4. Commits the work locally and reports a summary
// ─────────────────────────────────────────────────────────────
class UiDeveloper : public QObject {
    Q_OBJECT

public:
    enum class Framework {
        AutoDetect,
        HtmlCssJs,
        React,
        Vue,
        QtCpp,
        Flutter,
        Python,
    };

    struct GeneratedFile {
        QString relativePath;
        QString content;
    };

    explicit UiDeveloper(QObject *parent = nullptr);

    // ── Main entry point ──────────────────────────────────────
    // Called after the LLM has already produced its raw text.
    // Parses files, creates branch, writes files, commits.
    void implementFromLlmOutput(const QString &llmOutput,
                                const QString &projectDirectory,
                                const QString &branchName);

    // ── Framework detection ───────────────────────────────────
    static Framework detectFramework(const QString &projectDirectory);
    static QString frameworkName(Framework fw);

    // ── Prompt builder ────────────────────────────────────────
    static QString buildUiGenerationPrompt(const QString &requirements,
                                           Framework framework,
                                           const QString &projectDirectory);

    // ── State ─────────────────────────────────────────────────
    [[nodiscard]] bool isBusy() const { return m_busy; }

signals:
    void progress(const QString &message);
    void finished(bool success, const QString &summary, const QString &branchName);

private:
    // Git helpers (run synchronously, in-process)
    bool gitRun(const QString &workdir, const QStringList &args,
                QString *output = nullptr) const;
    bool branchExists(const QString &workdir, const QString &branch) const;
    QString currentBranch(const QString &workdir) const;
    bool createAndCheckoutBranch(const QString &workdir, const QString &branch);
    bool commitFiles(const QString &workdir, const QStringList &files,
                     const QString &message);

    // File parsing
    static QList<GeneratedFile> parseGeneratedFiles(const QString &llmOutput);

    bool m_busy{false};
};

#endif // TITANAI_UI_DEVELOPER_HPP
