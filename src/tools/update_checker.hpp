#ifndef TITANAI_UPDATE_CHECKER_HPP
#define TITANAI_UPDATE_CHECKER_HPP

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QList>

struct InstalledPackage {
    QString name;
    QString version;
};

struct PendingUpdate {
    QString name;
    QString currentVersion;
    QString newVersion;
    QString repository;

    [[nodiscard]] bool isAur() const { return repository == QLatin1String("AUR"); }
};

class UpdateChecker : public QObject {
    Q_OBJECT

public:
    explicit UpdateChecker(QObject *parent = nullptr);
    ~UpdateChecker() override;

    void startCheck();
    void cancelCheck();

    [[nodiscard]] bool isChecking() const { return m_checking; }
    [[nodiscard]] int installedPackageCount() const { return m_installed.size(); }
    [[nodiscard]] QList<InstalledPackage> installedPackages() const { return m_installed; }
    [[nodiscard]] QList<PendingUpdate> pendingUpdates() const { return m_updates; }
    [[nodiscard]] bool usesSafeUpdateChecker() const;
    [[nodiscard]] bool aurHelperAvailable() const { return !m_aurHelper.isEmpty(); }

    [[nodiscard]] QString formatUpdateReport() const;
    [[nodiscard]] QString formatInstalledSummary() const;

signals:
    void checkStarted();
    void checkProgress(const QString &stage);
    void checkFinished(int updateCount);
    void checkError(const QString &error);

private slots:
    void onInstalledFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onRepoUpdatesFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onAurUpdatesFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    void startProcessQueries();
    void checkCompletion();
    void finishCheck();
    void resetState();

    QList<InstalledPackage> m_installed;
    QList<PendingUpdate> m_updates;
    QString m_aurHelper;
    QString m_repoUpdateCommand;

    QProcess *m_installedProcess{nullptr};
    QProcess *m_repoUpdatesProcess{nullptr};
    QProcess *m_aurProcess{nullptr};
    bool m_installedDone{true};
    bool m_repoUpdatesDone{true};
    bool m_aurDone{true};
    bool m_checking{false};
};

#endif // TITANAI_UPDATE_CHECKER_HPP
