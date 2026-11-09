#ifndef TITANAI_APPLICATION_LAUNCHER_HPP
#define TITANAI_APPLICATION_LAUNCHER_HPP

#include <QList>
#include <QString>
#include <QStringList>

struct ApplicationInfo {
    QString id;
    QString name;
    QString executable;
    QString desktopFile;
    QStringList baseArguments;
    bool fromDesktopEntry{false};

    [[nodiscard]] bool isValid() const { return !executable.trimmed().isEmpty(); }
};

/// Resolves and launches desktop applications without invoking a shell.
///
/// Only system-trusted .desktop directories are scanned. Shell/interpreter
/// entries are rejected. Methods are virtual so tests can inject a mock.
class ApplicationLauncher {
public:
    ApplicationLauncher();
    virtual ~ApplicationLauncher();

    [[nodiscard]] virtual QList<ApplicationInfo> applications() const;
    [[nodiscard]] virtual ApplicationInfo resolve(const QString &name,
                                                 QString *error = nullptr) const;
    [[nodiscard]] virtual bool isInstalled(const QString &name) const;
    [[nodiscard]] virtual bool launch(const ApplicationInfo &application,
                                      const QStringList &extraArguments,
                                      const QString &workingDirectory,
                                      QString *error = nullptr);
    [[nodiscard]] virtual bool openDirectory(const QString &path,
                                             QString *error = nullptr);
    [[nodiscard]] virtual bool openFile(const QString &path,
                                       QString *error = nullptr);

    [[nodiscard]] static QStringList trustedDesktopDirectories();
    [[nodiscard]] static QList<ApplicationInfo> parseDesktopDirectory(const QString &directory);

    void setSearchPathsForTesting(const QStringList &directories);

private:
    [[nodiscard]] QStringList searchPaths() const;
    [[nodiscard]] QList<ApplicationInfo> scan() const;

    QStringList m_searchPathsOverride;
    mutable bool m_cacheValid{false};
    mutable QList<ApplicationInfo> m_cache;
};

#endif // TITANAI_APPLICATION_LAUNCHER_HPP
