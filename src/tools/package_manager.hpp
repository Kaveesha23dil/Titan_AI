#ifndef TITANAI_PACKAGE_MANAGER_HPP
#define TITANAI_PACKAGE_MANAGER_HPP

#include <QObject>
#include <QProcess>
#include <QStringList>

class PackageManager : public QObject {
    Q_OBJECT

public:
    explicit PackageManager(QObject *parent = nullptr);
    ~PackageManager() override;

    [[nodiscard]] bool isBusy() const;

public slots:
    void install(const QStringList &packages);

signals:
    void outputReceived(const QString &line);
    void finished(bool success, const QString &summary);

private slots:
    void readOutput();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QString buildSummary(bool success) const;
    void emitResult(bool success, const QString &summary);

    QProcess *m_process{nullptr};
    QStringList m_packages;
    QByteArray m_log;
    bool m_finishedEmitted{false};
};

#endif // TITANAI_PACKAGE_MANAGER_HPP
