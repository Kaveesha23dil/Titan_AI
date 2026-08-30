#ifndef TITANAI_UPDATE_CHECKER_HPP
#define TITANAI_UPDATE_CHECKER_HPP

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QList>
#include <QTimer>
#include <QDateTime>

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

    // --- Manual check ---
    void startCheck();
    void cancelCheck();

    // --- Periodic auto-check (background) ---
    void startPeriodicCheck(int intervalMinutes = 30);
    void stopPeriodicCheck();
    [[nodiscard]] bool isPeriodicCheckActive() const;

    // --- Apply updates ---
    // Launches update command in a visible terminal so the user can auth + watch output.
    void applyUpdates(bool aurToo = false);
    [[nodiscard]] bool isApplying() const { return m_applying; }

    // --- State accessors ---
    [[nodiscard]] bool isChecking() const { return m_checking; }
    [[nodiscard]] int installedPackageCount() const { return m_installed.size(); }
    [[nodiscard]] QList<InstalledPackage> installedPackages() const { return m_installed; }
    [[nodiscard]] QList<PendingUpdate>    pendingUpdates()    const { return m_updates; }
    [[nodiscard]] bool     usesSafeUpdateChecker() const;
    [[nodiscard]] bool     aurHelperAvailable()    const { return !m_aurHelper.isEmpty(); }
    [[nodiscard]] QDateTime lastCheckTime()        const { return m_lastCheckTime; }
    [[nodiscard]] QString   lastCheckTimeString()  const;

    // --- Formatted output ---
    [[nodiscard]] QString formatUpdateReport()     const;
    [[nodiscard]] QString formatInstalledSummary() const;

signals:
    void checkStarted();
    void checkProgress(const QString &stage);
    void checkFinished(int updateCount);
    void checkError(const QString &error);

    // Apply-updates signals
    void updatesApplyStarted(const QString &command);
    void updatesApplyOutput(const QString &line);   // live stdout/stderr
    void updatesApplyFinished(bool success);
    void updatesApplyError(const QString &error);

    // Fired after a periodic (background) check completes
    void periodicCheckDone(int updateCount);

private slots:
    void onInstalledFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onRepoUpdatesFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onAurUpdatesFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onApplyReadyRead();
    void onApplyFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onPeriodicTimerFired();

private:
    void startProcessQueries();
    void checkCompletion();
    void finishCheck();
    void resetState();
    QString findTerminalEmulator() const;

    QList<InstalledPackage> m_installed;
    QList<PendingUpdate>    m_updates;
    QString   m_aurHelper;
    QString   m_repoUpdateCommand;
    QDateTime m_lastCheckTime;
    bool      m_periodicCheckActive{false};
    bool      m_isPeriodicFire{false};

    QProcess *m_installedProcess{nullptr};
    QProcess *m_repoUpdatesProcess{nullptr};
    QProcess *m_aurProcess{nullptr};
    QProcess *m_applyProcess{nullptr};
    QTimer    m_periodicTimer;

    bool m_installedDone{true};
    bool m_repoUpdatesDone{true};
    bool m_aurDone{true};
    bool m_checking{false};
    bool m_applying{false};
};

#endif // TITANAI_UPDATE_CHECKER_HPP
