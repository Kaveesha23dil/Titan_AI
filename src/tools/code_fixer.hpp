#ifndef TITANAI_CODE_FIXER_HPP
#define TITANAI_CODE_FIXER_HPP

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

class QTimer;

class CodeFixer : public QObject {
    Q_OBJECT

public:
    struct BuildError {
        QString file;
        int line{0};
        int column{0};
        QString message;
    };

    struct FixEdit {
        QString file;
        int startLine{0};
        int endLine{0};
        QString replacement;
    };

    explicit CodeFixer(QObject *parent = nullptr);
    ~CodeFixer() override;

    [[nodiscard]] bool isBusy() const;

    static QList<BuildError> parseErrors(const QString &output);
    static QString resolveProjectRoot(const QString &startDir);
    static QString readContextForFile(const QString &absoluteFilePath,
                                      const QList<BuildError> &fileErrors);
    static QList<FixEdit> parseFixes(const QString &llmOutput);
    static bool applyEdit(const QString &absoluteFilePath, const FixEdit &edit,
                          QString *errorOut);

public slots:
    void runBuild(const QString &workdir, const QString &command);

signals:
    void buildFinished(bool success, const QString &output);

private slots:
    void readBuildOutput();
    void onBuildFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onBuildTimeout();

private:
    QProcess *m_buildProcess{nullptr};
    QByteArray m_buildLog;
    bool m_buildDone{false};
    QTimer *m_buildTimeout{nullptr};
};

#endif // TITANAI_CODE_FIXER_HPP
