#include "learning/task_tracker.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDateTime>
#include <QTime>
#include <QSet>
#include <QRegularExpression>
#include <QCoreApplication>

#include <unistd.h>
#include <sys/stat.h>

TaskTracker::TaskTracker(QObject *parent)
    : QObject(parent)
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    m_dataPath = dataDir + QStringLiteral("/task_history.json");

    connect(&m_pollTimer, &QTimer::timeout, this, &TaskTracker::pollProcesses);
}

void TaskTracker::startTracking()
{
    if (m_tracking) {
        return;
    }

    loadHistory();
    m_tracking = true;
    m_pollTimer.start(30000);

    QTimer::singleShot(0, this, &TaskTracker::pollProcesses);
}

void TaskTracker::stopTracking()
{
    if (!m_tracking) {
        return;
    }

    m_pollTimer.stop();
    m_tracking = false;
    saveHistory();
}

bool TaskTracker::isTracking() const
{
    return m_tracking;
}

void TaskTracker::setTrackingInterval(int intervalMs)
{
    if (m_pollTimer.isActive()) {
        m_pollTimer.setInterval(intervalMs);
    }
}

void TaskTracker::setDataFilePath(const QString &path)
{
    m_dataPath = path;
}

QString TaskTracker::dataFilePath() const
{
    return m_dataPath;
}

QList<TaskEntry> TaskTracker::recentEntries(int count) const
{
    QList<TaskEntry> result;
    const int start = qMax(0, m_entries.size() - count);
    for (int i = start; i < m_entries.size(); ++i) {
        result.append(m_entries.at(i));
    }
    return result;
}

QList<TaskEntry> TaskTracker::entriesForDate(const QDate &date) const
{
    QList<TaskEntry> result;
    const qint64 dayStart = QDateTime(date, QTime(0, 0, 0)).toMSecsSinceEpoch();
    const qint64 dayEnd = QDateTime(date.addDays(1), QTime(0, 0, 0)).toMSecsSinceEpoch();

    for (const TaskEntry &entry : m_entries) {
        if (entry.timestampMs >= dayStart && entry.timestampMs < dayEnd) {
            result.append(entry);
        }
    }
    return result;
}

QJsonArray TaskTracker::entriesToJson(const QList<TaskEntry> &entries) const
{
    QJsonArray arr;
    for (const TaskEntry &entry : entries) {
        arr.append(entryToJson(entry));
    }
    return arr;
}

QJsonObject TaskTracker::entryToJson(const TaskEntry &entry) const
{
    QJsonObject obj;
    obj[QStringLiteral("process")] = entry.processName;
    obj[QStringLiteral("command")] = entry.commandLine;
    obj[QStringLiteral("window")] = entry.windowTitle;
    obj[QStringLiteral("category")] = entry.category;
    obj[QStringLiteral("timestamp")] = entry.timestampMs;
    obj[QStringLiteral("duration")] = entry.durationMs;
    return obj;
}

TaskEntry TaskTracker::jsonToEntry(const QJsonObject &obj) const
{
    TaskEntry entry;
    entry.processName = obj[QStringLiteral("process")].toString();
    entry.commandLine = obj[QStringLiteral("command")].toString();
    entry.windowTitle = obj[QStringLiteral("window")].toString();
    entry.category = obj[QStringLiteral("category")].toString();
    entry.timestampMs = obj[QStringLiteral("timestamp")].toInteger();
    entry.durationMs = obj[QStringLiteral("duration")].toInt();
    return entry;
}

QString TaskTracker::categorizeProcess(const QString &name, const QString &cmdline) const
{
    const QString lower = name.toLower();
    const QString cmdLower = cmdline.toLower();

    static const QMap<QString, QStringList> categoryMap = {
        {QStringLiteral("development"), {
            QStringLiteral("code"), QStringLiteral("vscode"), QStringLiteral("vim"),
            QStringLiteral("nvim"), QStringLiteral("emacs"), QStringLiteral("sublime"),
            QStringLiteral("qtcreator"), QStringLiteral("clion"), QStringLiteral("idea"),
            QStringLiteral("git"), QStringLiteral("make"), QStringLiteral("cmake"),
            QStringLiteral("gcc"), QStringLiteral("g++"), QStringLiteral("clang"),
            QStringLiteral("cargo"), QStringLiteral("rustc"), QStringLiteral("node"),
            QStringLiteral("npm"), QStringLiteral("python"), QStringLiteral("pip"),
            QStringLiteral("java"), QStringLiteral("javac"), QStringLiteral("gradle"),
            QStringLiteral("mvn"), QStringLiteral("dotnet"), QStringLiteral("docker"),
        }},
        {QStringLiteral("browsing"), {
            QStringLiteral("firefox"), QStringLiteral("chromium"), QStringLiteral("chrome"),
            QStringLiteral("brave"), QStringLiteral("vivaldi"), QStringLiteral("opera"),
            QStringLiteral("midori"), QStringLiteral("lynx"),
        }},
        {QStringLiteral("terminal"), {
            QStringLiteral("bash"), QStringLiteral("zsh"), QStringLiteral("fish"),
            QStringLiteral("sh"), QStringLiteral("tmux"), QStringLiteral("screen"),
            QStringLiteral("alacritty"), QStringLiteral("kitty"), QStringLiteral("wezterm"),
            QStringLiteral("foot"), QStringLiteral("st"), QStringLiteral("urxvt"),
            QStringLiteral("konsole"), QStringLiteral("gnome-terminal"),
        }},
        {QStringLiteral("media"), {
            QStringLiteral("mpv"), QStringLiteral("vlc"), QStringLiteral("spotify"),
            QStringLiteral("rhythmbox"), QStringLiteral("audacious"), QStringLiteral("cmus"),
            QStringLiteral("ffmpeg"), QStringLiteral("obs"), QStringLiteral("gimp"),
            QStringLiteral("inkscape"), QStringLiteral("blender"), QStringLiteral("krita"),
        }},
        {QStringLiteral("office"), {
            QStringLiteral("libreoffice"), QStringLiteral("writer"), QStringLiteral("calc"),
            QStringLiteral("impress"), QStringLiteral("obsidian"), QStringLiteral("notion"),
            QStringLiteral("evince"), QStringLiteral("okular"), QStringLiteral("zathura"),
            QStringLiteral("thunderbird"), QStringLiteral("evolution"),
        }},
        {QStringLiteral("communication"), {
            QStringLiteral("telegram"), QStringLiteral("discord"), QStringLiteral("slack"),
            QStringLiteral("signal"), QStringLiteral("zoom"), QStringLiteral("teams"),
            QStringLiteral("mattermost"), QStringLiteral("element"),
        }},
        {QStringLiteral("system"), {
            QStringLiteral("systemctl"), QStringLiteral("pacman"), QStringLiteral("paru"),
            QStringLiteral("yay"), QStringLiteral("htop"), QStringLiteral("btop"),
            QStringLiteral("neofetch"), QStringLiteral("top"), QStringLiteral("nmcli"),
            QStringLiteral("journalctl"), QStringLiteral("dmesg"),
        }},
    };

    for (auto it = categoryMap.constBegin(); it != categoryMap.constEnd(); ++it) {
        for (const QString &keyword : it.value()) {
            if (lower.contains(keyword) || cmdLower.contains(keyword)) {
                return it.key();
            }
        }
    }

    return QStringLiteral("other");
}

QString TaskTracker::detectActiveWindow() const
{
    const QString wmClass = QStringLiteral(
        "xdotool getactivewindow getwindowname 2>/dev/null");
    FILE *pipe = popen(wmClass.toUtf8().constData(), "r");
    if (!pipe) {
        return QString();
    }

    char buffer[1024];
    QString result;
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result = QString::fromUtf8(buffer).trimmed();
    }
    pclose(pipe);
    return result;
}

QList<QString> TaskTracker::getRunningProcesses() const
{
    QList<QString> processes;

    FILE *pipe = popen("ps -eo pid,comm --no-headers 2>/dev/null", "r");
    if (!pipe) {
        return processes;
    }

    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        QString line = QString::fromUtf8(buffer).trimmed();
        QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")));
        if (parts.size() >= 2) {
            processes.append(parts.first());
        }
    }
    pclose(pipe);
    return processes;
}

QString TaskTracker::extractProcessName(const QString &rawName) const
{
    QString name = rawName;
    int lastSlash = name.lastIndexOf(QLatin1Char('/'));
    if (lastSlash >= 0) {
        name = name.mid(lastSlash + 1);
    }
    return name;
}

QString TaskTracker::extractCommandLine(const QString &pid) const
{
    const QString path = QStringLiteral("/proc/%1/cmdline").arg(pid);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }

    QByteArray data = file.read(4096);
    file.close();

    data.replace('\0', ' ');
    return QString::fromUtf8(data).trimmed().left(200);
}

bool TaskTracker::shouldRecord(const QString &processName, const QString & /*cmdline*/) const
{
    const QString lower = processName.toLower();

    static const QSet<QString> ignoreList = {
        QStringLiteral("ps"), QStringLiteral("grep"), QStringLiteral("awk"),
        QStringLiteral("sed"), QStringLiteral("head"), QStringLiteral("tail"),
        QStringLiteral("cat"), QStringLiteral("less"), QStringLiteral("more"),
        QStringLiteral("top"), QStringLiteral("htop"), QStringLiteral("btop"),
        QStringLiteral("systemd"), QStringLiteral("dbus-daemon"),
        QStringLiteral("Xorg"), QStringLiteral("xwayland"),
        QStringLiteral("sway"), QStringLiteral("i3"), QStringLiteral("hyprland"),
        QStringLiteral("kwin_wayland"), QStringLiteral("kwin_x11"),
        QStringLiteral("gnome-shell"), QStringLiteral("plasmashell"),
        QStringLiteral("pulseaudio"), QStringLiteral("pipewire"),
        QStringLiteral("titanai"), QStringLiteral("titanserver"),
    };

    if (ignoreList.contains(lower)) {
        return false;
    }

    if (lower.startsWith(QStringLiteral("kworker")) ||
        lower.startsWith(QStringLiteral("rcu_")) ||
        lower.startsWith(QStringLiteral("migration-")) ||
        lower.startsWith(QStringLiteral("watchdog"))) {
        return false;
    }

    return true;
}

void TaskTracker::pollProcesses()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QString activeWindow = detectActiveWindow();

    QList<QPair<QString, QString>> runningApps;

    FILE *pipe = popen("ps -eo pid,comm --no-headers 2>/dev/null", "r");
    if (!pipe) {
        return;
    }

    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        QString line = QString::fromUtf8(buffer).trimmed();
        QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")));
        if (parts.size() < 2) {
            continue;
        }

        QString pid = parts.first();
        QString rawName = parts.mid(1).join(QStringLiteral(" "));
        QString name = extractProcessName(rawName);

        if (!shouldRecord(name, QString())) {
            continue;
        }

        QString cmdline = extractCommandLine(pid);
        QString category = categorizeProcess(name, cmdline);

        static const QStringList appCategories = {
            QStringLiteral("development"),
            QStringLiteral("browsing"),
            QStringLiteral("media"),
            QStringLiteral("office"),
            QStringLiteral("communication"),
        };

        if (appCategories.contains(category)) {
            runningApps.append({name, cmdline});
        }
    }
    pclose(pipe);

    QSet<QString> seenProcesses;

    for (const auto &[name, cmdline] : runningApps) {
        if (seenProcesses.contains(name)) {
            continue;
        }
        seenProcesses.insert(name);

        if (name == m_lastProcessName) {
            continue;
        }

        if (!m_lastProcessName.isEmpty() && m_lastProcessStartMs > 0) {
            int duration = static_cast<int>(now - m_lastProcessStartMs);
            if (duration > 5000 && duration < 3600000) {
                TaskEntry prevEntry;
                prevEntry.processName = m_lastProcessName;
                prevEntry.commandLine = QString();
                prevEntry.windowTitle = QString();
                prevEntry.category = categorizeProcess(m_lastProcessName, QString());
                prevEntry.timestampMs = m_lastProcessStartMs;
                prevEntry.durationMs = duration;
                recordEntry(prevEntry);
            }
        }

        TaskEntry entry;
        entry.processName = name;
        entry.commandLine = cmdline;
        entry.windowTitle = activeWindow;
        entry.category = categorizeProcess(name, cmdline);
        entry.timestampMs = now;
        entry.durationMs = 0;
        recordEntry(entry);

        m_lastProcessName = name;
        m_lastProcessStartMs = now;

        break;
    }

    if (m_entries.size() > m_maxEntries) {
        m_entries = m_entries.mid(m_entries.size() - m_maxEntries);
    }
}

void TaskTracker::scanActiveWindow()
{
    const QString window = detectActiveWindow();
    if (!window.isEmpty() && !m_lastProcessName.isEmpty()) {
        TaskEntry entry;
        entry.processName = m_lastProcessName;
        entry.windowTitle = window;
        entry.category = categorizeProcess(m_lastProcessName, window);
        entry.timestampMs = QDateTime::currentMSecsSinceEpoch();
        entry.durationMs = 0;
        recordEntry(entry);
    }
}

void TaskTracker::recordEntry(const TaskEntry &entry)
{
    m_entries.append(entry);
    emit taskDetected(entry);

    if (m_entries.size() % 50 == 0) {
        saveHistory();
    }
}

void TaskTracker::loadHistory()
{
    QFile file(m_dataPath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        return;
    }

    const QJsonArray arr = doc.array();
    m_entries.clear();
    m_entries.reserve(arr.size());

    for (const QJsonValue &val : arr) {
        if (val.isObject()) {
            m_entries.append(jsonToEntry(val.toObject()));
        }
    }
}

void TaskTracker::saveHistory()
{
    QFile file(m_dataPath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit trackingError(
            QStringLiteral("Failed to save task history to %1").arg(m_dataPath));
        return;
    }

    QJsonArray arr;
    const int start = qMax(0, m_entries.size() - m_maxEntries);
    for (int i = start; i < m_entries.size(); ++i) {
        arr.append(entryToJson(m_entries.at(i)));
    }

    file.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    file.close();
}
