#include "tools/system_info.hpp"

#include <QFile>
#include <QSysInfo>
#include <QStringList>
#include <QList>
#include <QByteArray>

#include <sys/utsname.h>
#include <unistd.h>

QString SystemInfoTool::getOperatingSystem() const
{
    QString osName;

    auto parseOsRelease = [](const QString &filePath) -> QString {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            return QString();
        }

        QByteArray data = file.readAll();
        file.close();

        QString prettyName;
        QString name;

        QList<QByteArray> lines = data.split('\n');
        for (const QByteArray &rawLine : lines) {
            QString line = QString::fromUtf8(rawLine).trimmed();
            if (line.startsWith(QStringLiteral("PRETTY_NAME="))) {
                prettyName = line.section('=', 1).trimmed();
                if ((prettyName.startsWith('"') && prettyName.endsWith('"')) ||
                    (prettyName.startsWith('\'') && prettyName.endsWith('\''))) {
                    prettyName = prettyName.mid(1, prettyName.length() - 2);
                }
            } else if (line.startsWith(QStringLiteral("NAME="))) {
                name = line.section('=', 1).trimmed();
                if ((name.startsWith('"') && name.endsWith('"')) ||
                    (name.startsWith('\'') && name.endsWith('\''))) {
                    name = name.mid(1, name.length() - 2);
                }
            }
        }

        return !prettyName.isEmpty() ? prettyName : name;
    };

    osName = parseOsRelease(QStringLiteral("/etc/os-release"));
    if (osName.isEmpty()) {
        osName = parseOsRelease(QStringLiteral("/usr/lib/os-release"));
    }
    if (osName.isEmpty()) {
        osName = QSysInfo::prettyProductName();
    }

    return osName;
}

QString SystemInfoTool::getKernelVersion() const
{
    struct utsname buf;
    if (uname(&buf) == 0) {
        return QString::fromUtf8(buf.release);
    }

    QFile file(QStringLiteral("/proc/sys/kernel/osrelease"));
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray data = file.readAll();
        file.close();
        QString version = QString::fromUtf8(data).trimmed();
        if (!version.isEmpty()) {
            return version;
        }
    }

    return QSysInfo::kernelVersion();
}

QString SystemInfoTool::getHostname() const
{
    struct utsname buf;
    if (uname(&buf) == 0) {
        return QString::fromUtf8(buf.nodename);
    }

    char hostnameBuf[256];
    if (gethostname(hostnameBuf, sizeof(hostnameBuf)) == 0) {
        return QString::fromUtf8(hostnameBuf);
    }

    return QStringLiteral("localhost");
}

QString SystemInfoTool::getArchitecture() const
{
    struct utsname buf;
    if (uname(&buf) == 0) {
        return QString::fromUtf8(buf.machine);
    }

    return QSysInfo::currentCpuArchitecture();
}

QString SystemInfoTool::getCpuModel() const
{
    QFile file(QStringLiteral("/proc/cpuinfo"));
    if (!file.open(QIODevice::ReadOnly)) {
        return QStringLiteral("Unknown CPU");
    }

    QByteArray data = file.readAll();
    file.close();

    QList<QByteArray> lines = data.split('\n');
    for (const QByteArray &rawLine : lines) {
        QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.startsWith(QStringLiteral("model name"), Qt::CaseInsensitive) ||
            line.startsWith(QStringLiteral("Hardware"), Qt::CaseInsensitive)) {
            int colonIdx = line.indexOf(':');
            if (colonIdx != -1) {
                return line.mid(colonIdx + 1).trimmed();
            }
        }
    }

    return QStringLiteral("Unknown CPU");
}

int SystemInfoTool::getCpuCoreCount() const
{
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores > 0) {
        return static_cast<int>(cores);
    }

    QFile file(QStringLiteral("/proc/cpuinfo"));
    if (!file.open(QIODevice::ReadOnly)) {
        return 1;
    }

    QByteArray data = file.readAll();
    file.close();

    int count = 0;
    QList<QByteArray> lines = data.split('\n');
    for (const QByteArray &rawLine : lines) {
        QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.startsWith(QStringLiteral("processor"), Qt::CaseInsensitive)) {
            count++;
        }
    }

    return count > 0 ? count : 1;
}

quint64 SystemInfoTool::getTotalMemoryBytes() const
{
    QFile file(QStringLiteral("/proc/meminfo"));
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }

    QByteArray data = file.readAll();
    file.close();

    QList<QByteArray> lines = data.split('\n');
    for (const QByteArray &rawLine : lines) {
        QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.startsWith(QStringLiteral("MemTotal:"))) {
            QStringList parts = line.split(QChar(' '), Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                bool ok = false;
                quint64 val = parts[1].toULongLong(&ok);
                if (ok) {
                    return val * 1024ULL;
                }
            }
        }
    }

    return 0;
}

quint64 SystemInfoTool::getAvailableMemoryBytes() const
{
    QFile file(QStringLiteral("/proc/meminfo"));
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }

    QByteArray data = file.readAll();
    file.close();

    quint64 availableKb = 0;
    quint64 memFreeKb = 0;
    quint64 buffersKb = 0;
    quint64 cachedKb = 0;
    bool foundAvailable = false;

    QList<QByteArray> lines = data.split('\n');
    for (const QByteArray &rawLine : lines) {
        QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.startsWith(QStringLiteral("MemAvailable:"))) {
            QStringList parts = line.split(QChar(' '), Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                bool ok = false;
                availableKb = parts[1].toULongLong(&ok);
                if (ok) {
                    foundAvailable = true;
                    break;
                }
            }
        } else if (line.startsWith(QStringLiteral("MemFree:"))) {
            QStringList parts = line.split(QChar(' '), Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                memFreeKb = parts[1].toULongLong();
            }
        } else if (line.startsWith(QStringLiteral("Buffers:"))) {
            QStringList parts = line.split(QChar(' '), Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                buffersKb = parts[1].toULongLong();
            }
        } else if (line.startsWith(QStringLiteral("Cached:"))) {
            QStringList parts = line.split(QChar(' '), Qt::SkipEmptyParts);
            if (parts.size() >= 2) {
                cachedKb = parts[1].toULongLong();
            }
        }
    }

    if (foundAvailable) {
        return availableKb * 1024ULL;
    }

    return (memFreeKb + buffersKb + cachedKb) * 1024ULL;
}

SystemInfo SystemInfoTool::getSystemInfo() const
{
    SystemInfo info;
    info.operatingSystem = getOperatingSystem();
    info.kernelVersion = getKernelVersion();
    info.hostname = getHostname();
    info.cpuModel = getCpuModel();
    info.cpuCoreCount = getCpuCoreCount();
    info.totalMemoryBytes = getTotalMemoryBytes();
    info.availableMemoryBytes = getAvailableMemoryBytes();
    info.architecture = getArchitecture();
    return info;
}
